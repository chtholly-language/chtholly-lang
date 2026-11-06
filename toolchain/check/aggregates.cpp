// SPDX-License-Identifier: MIT OR Apache-2.0
#include "checker.h"
#include <algorithm>
#include <functional>
#include <unordered_set>

namespace Chtholly::Check {
using Type = Types::TypeId;
using Id = SemIR::Id;
namespace {
auto ConstantSyntax(const Parse::Tree &tree, Id id,
                    const std::function<bool(std::string)> &constantName,
                    const Source::Buffer &source) -> bool {
  const auto spelling = [&](Source::Span text) {
    auto word = source.slice(text);
    if (word.starts_with('`')) {
      word.remove_prefix(1);
      word.remove_suffix(1);
    }
    return std::string(word);
  };
  std::function<bool(Id, std::unordered_set<std::string>)> eligible;
  eligible = [&](Id expression, std::unordered_set<std::string> locals) {
    const auto &node = tree.Nodes.at(expression);
    switch (node.Form) {
    case Parse::Kind::Integer:
    case Parse::Kind::Float:
    case Parse::Kind::Boolean:
    case Parse::Kind::Unit:
      return true;
    case Parse::Kind::Name:
      return locals.contains(spelling(node.Text)) ||
             constantName(spelling(node.Text));
    case Parse::Kind::Group:
    case Parse::Kind::Unary:
    case Parse::Kind::Convert:
      return eligible(node.Left, locals);
    case Parse::Kind::Binary:
      return eligible(node.Left, locals) && eligible(node.Right, locals);
    case Parse::Kind::If:
      return eligible(node.Left, locals) && eligible(node.Right, locals) &&
             (node.Alternative == Parse::NoNode ||
              eligible(node.Alternative, locals));
    case Parse::Kind::Block:
      for (const auto statement : node.Statements) {
        const auto &item = tree.Nodes[statement];
        if (item.Form == Parse::Kind::Discard) {
          if (!eligible(item.Left, locals)) {
            return false;
          }
        } else if (item.Form == Parse::Kind::Bind && !item.Mutable &&
                   item.Pattern != Parse::NoPattern &&
                   tree.Patterns[item.Pattern].Form ==
                       Parse::PatternKind::Binding &&
                   !tree.Patterns[item.Pattern].Mutable) {
          if (!eligible(item.Left, locals) ||
              (item.Alternative != Parse::NoNode &&
               !eligible(item.Alternative, locals))) {
            return false;
          }
          locals.insert(spelling(tree.Patterns[item.Pattern].Text));
        } else {
          return false;
        }
      }
      return node.Left == Parse::NoNode || eligible(node.Left, locals);
    default:
      return false;
    }
  };
  return eligible(id, {});
}
} // namespace
auto Checker::aggregate(Id id, Type context, bool executable) -> Type {
  const auto &syntax = Tree.Nodes[id];
  auto &record = Result.Nodes[id];
  record.Form = SemIR::Kind::Construct;
  record.Statements = syntax.Statements;
  std::vector<Types::Field> fields;
  Type element = Type::Invalid;
  std::uint64_t count = 0;
  if (syntax.Form == Parse::Kind::Construct) {
    const auto spelling = name(syntax.Text);
    if (lookup(spelling) || Constants.contains(spelling)) {
      fail(id, "nearest value binding shadows this constructor type");
    }
    if (const auto found = TypeNames.find(spelling); found != TypeNames.end()) {
      record.ValueType = found->second;
    } else if (const auto found = Aliases.find(spelling);
               found != Aliases.end()) {
      record.ValueType = resolveAlias(found->second);
    } else {
      fail(id, "unknown constructor type");
    }
    if (record.ValueType.owner() == nullptr) {
      fail(id, "constructor requires a nominal struct or enum");
      return Type::Invalid;
    }
    const auto declaration = TypeBuilder.get(record.ValueType);
    if (declaration.Form == Types::Kind::Enum) {
      const auto variantName = name(syntax.Member);
      for (Id i = 0; i < declaration.Variants.size(); ++i) {
        if (declaration.Variants[i].Name == variantName) {
          record.Entity = i;
          fields = declaration.Variants[i].Fields;
        }
      }
      if (record.Entity == SemIR::None) {
        fail(id, "unknown enum variant");
      }
    } else if (declaration.Form == Types::Kind::Struct &&
               syntax.Member.Length == 0) {
      fields = declaration.Fields;
    } else {
      fail(id, "constructor does not name a struct or qualified enum variant");
    }
  } else if (syntax.Form == Parse::Kind::Tuple) {
    if (context.owner() != nullptr &&
        TypeBuilder.get(context).Form == Types::Kind::Tuple) {
      fields = TypeBuilder.get(context).Fields;
    }
  } else {
    if (context.owner() != nullptr &&
        TypeBuilder.get(context).Form == Types::Kind::Array) {
      element = TypeBuilder.get(context).Element;
    }
    if (element == Type::Invalid && syntax.Form == Parse::Kind::Array) {
      for (const auto item : syntax.Statements) {
        const auto known = hint(item);
        if (known != Type::Invalid && known != Type::Never) {
          element = known;
          break;
        }
      }
    }
    if (syntax.Form == Parse::Kind::Repeat) {
      const auto lengthValue = length(syntax.Right);
      count = lengthValue.value_or(0);
      record.Repeat = true;
      record.Count = count;
      record.Statements = {syntax.Left};
    } else {
      count = syntax.Statements.size();
    }
  }
  std::vector<Type> elements;
  std::unordered_set<Id> provided;
  bool completes = true;
  for (Id i = 0; i < record.Statements.size(); ++i) {
    Id slot = i;
    Type expected = Type::Invalid;
    if (syntax.Form == Parse::Kind::Construct) {
      slot = SemIR::None;
      const auto spelling = name(syntax.Fields[i]);
      for (Id field = 0; field < fields.size(); ++field) {
        if (fields[field].Name == spelling) {
          slot = field;
        }
      }
      if (slot == SemIR::None || !provided.insert(slot).second) {
        fail(id, "unknown or duplicate constructor field");
      } else {
        expected = fields[slot].Type;
      }
    } else if (syntax.Form == Parse::Kind::Tuple) {
      if (i < fields.size()) {
        expected = fields[i].Type;
      }
    } else {
      expected = element;
    }
    const auto actual =
        expression(record.Statements[i], expected, executable && completes);
    completes &= Result.Nodes[record.Statements[i]].Completes;
    elements.push_back(actual);
    record.Slots.push_back(slot);
    if (syntax.Form == Parse::Kind::Array ||
        syntax.Form == Parse::Kind::Repeat) {
      if (element == Type::Invalid && actual != Type::Never) {
        element = actual;
      }
      if (actual != Type::Never && element != Type::Invalid &&
          actual != element) {
        fail(id, "array elements must have the same type");
      }
    }
  }
  if (syntax.Form == Parse::Kind::Construct) {
    if (provided.size() != fields.size()) {
      fail(id, "constructor must provide every field exactly once");
    }
  } else if (syntax.Form == Parse::Kind::Tuple) {
    record.ValueType = TypeBuilder.tuple(std::move(elements));
  } else {
    if (element == Type::Invalid) {
      if (record.Statements.empty()) {
        fail(id, "empty array requires an element type context");
      } else {
        element = Result.Nodes[record.Statements[0]].ValueType;
      }
    }
    record.ValueType = TypeBuilder.array(element, count);
    if (record.Repeat && count > 1 && element != Type::Invalid &&
        !TypeBuilder.copy(element)) {
      fail(id, "repeating more than one element requires Copy");
    }
  }
  record.Completes = completes;
  expect(id, context);
  return record.ValueType;
}
auto Checker::projection(Id id, bool executable) -> Type {
  const auto &syntax = Tree.Nodes[id];
  auto &record = Result.Nodes[id];
  record.Left = syntax.Left;
  const auto baseType =
      expression(syntax.Left, Type::Invalid, executable, true);
  const auto &base = Result.Nodes[syntax.Left];
  record.ValueCategory = base.ValueCategory;
  record.Writable = base.Writable;
  record.Completes = base.Completes;
  if (baseType.owner() == nullptr) {
    fail(id, "projection requires an aggregate type");
    return Type::Invalid;
  }
  const auto aggregate = TypeBuilder.get(baseType);
  if (syntax.Form == Parse::Kind::Project) {
    const auto spelling = name(syntax.Member);
    if (aggregate.Form == Types::Kind::Array && spelling == "len") {
      record.Form = SemIR::Kind::Length;
      record.ValueCategory = SemIR::Category::Value;
      record.Writable = false;
      record.ValueType = Type::USize;
      record.Count = aggregate.Length;
      // Do not set Constant: the base expression must still be evaluated.
      return record.ValueType;
    }
    record.Form = SemIR::Kind::Project;
    if (aggregate.Form != Types::Kind::Struct &&
        aggregate.Form != Types::Kind::Tuple) {
      fail(id, "enum payloads can only be projected through a checked pattern");
      return Type::Invalid;
    }
    for (Id i = 0; i < aggregate.Fields.size(); ++i) {
      if (aggregate.Fields[i].Name == spelling) {
        record.Entity = i;
        record.ValueType = aggregate.Fields[i].Type;
      }
    }
    if (record.Entity == SemIR::None) {
      fail(id, "unknown field or tuple projection");
    }
  } else {
    record.Form = SemIR::Kind::Index;
    record.Right = syntax.Right;
    if (aggregate.Form != Types::Kind::Array) {
      fail(id, "index requires a fixed array");
      return Type::Invalid;
    }
    const auto indexType =
        expression(syntax.Right, Type::Invalid, executable && base.Completes);
    if (!Types::IsInteger(indexType) && indexType != Type::Never) {
      fail(id, "array index must be an integer");
    }
    record.Completes &= Result.Nodes[syntax.Right].Completes;
    record.ValueType = aggregate.Element;
    record.Count = aggregate.Length;
    const auto constantName = [&](std::string spelling) {
      return Constants.contains(spelling) && !lookup(spelling);
    };
    if (ConstantSyntax(Tree, syntax.Right, constantName, Source)) {
      // This is an additional proof query, not a rewrite of the executable
      // SemIR. A pure block still needs its checked local IDs when Lower
      // evaluates it, even if the projection can use a fixed element path.
      const auto savedBudget = Budget;
      Budget = Numeric::Budget(Budget.limits());
      const auto evaluated = pure(syntax.Right, indexType, executable);
      Budget = savedBudget;
      if (!evaluated) {
        return record.ValueType;
      }
      const auto &value = *evaluated;
      const auto raw = value.bits();
      const auto width = Target::WindowsX64.bits(value.type());
      if (!Target::IsInteger(value.type()) ||
          (width != 8 && width != 16 && width != 32 && width != 64 &&
           width != 128)) {
        fail(id,
             "internal error: constant index has no integer representation");
        return record.ValueType;
      }
      const bool negative =
          Target::IsSigned(value.type()) &&
          (width == 128 ? (raw[1] >> 63) != 0 : (raw[0] >> (width - 1)) != 0);
      if (negative || raw[1] != 0 || raw[0] >= aggregate.Length) {
        fail(id, "constant array index is negative or out of bounds");
      } else {
        record.FixedIndex = raw[0];
      }
    }
  }
  return record.ValueType;
}
} // namespace Chtholly::Check
