// SPDX-License-Identifier: MIT OR Apache-2.0
#include "checker.h"

namespace Chtholly::Check {
// Record every leaf, even when no reliable type is available. An unknown
// inner binding must never fall back to an unrelated outer name. This helper
// only projects declared types; the checking pass validates pattern legality.
auto Checker::hintBindings(Parse::PatternId id, Type input,
                           std::unordered_map<std::string, Type> &locals)
    -> void {
  if (id == Parse::NoPattern) {
    return;
  }
  const auto &pattern = Tree.Patterns[id];
  if (pattern.Form == Parse::PatternKind::Binding) {
    const auto spelling = name(pattern.Text);
    if (spelling != "_") {
      locals[spelling] = input;
    }
    return;
  }
  if (pattern.Form == Parse::PatternKind::Group ||
      pattern.Form == Parse::PatternKind::Or) {
    if (!pattern.Children.empty()) {
      hintBindings(pattern.Children[0], input, locals);
    }
    return;
  }
  std::vector<Types::Field> fields;
  if (input.owner() != nullptr) {
    const auto &record = TypeBuilder.get(input);
    if ((pattern.Form == Parse::PatternKind::Tuple &&
         record.Form == Types::Kind::Tuple) ||
        (pattern.Form == Parse::PatternKind::Struct &&
         record.Form == Types::Kind::Struct)) {
      fields = record.Fields;
    } else if (pattern.Form == Parse::PatternKind::Variant &&
               record.Form == Types::Kind::Enum) {
      const auto variantName = name(pattern.Variant);
      for (const auto &variant : record.Variants) {
        if (variant.Name == variantName) {
          fields = variant.Fields;
          break;
        }
      }
    }
  }
  bool afterRest = false;
  for (std::size_t i = 0; i < pattern.Children.size(); ++i) {
    const auto child = pattern.Children[i];
    if (pattern.Form == Parse::PatternKind::Tuple &&
        Tree.Patterns[child].isTupleRest()) {
      afterRest = true;
      continue;
    }
    auto fieldType = Type::Invalid;
    if (pattern.Form == Parse::PatternKind::Tuple) {
      const auto remaining = pattern.Children.size() - i;
      const auto index = afterRest && remaining <= fields.size()
                             ? fields.size() - remaining
                             : i;
      if (index < fields.size()) {
        fieldType = fields[index].Type;
      }
    } else if (i < pattern.Fields.size()) {
      const auto fieldName = name(pattern.Fields[i]);
      for (const auto &field : fields) {
        if (field.Name == fieldName) {
          fieldType = field.Type;
          break;
        }
      }
    }
    hintBindings(child, fieldType, locals);
  }
}

// This walk only supplies already known types and binding-local defaults.
// It cannot grant operation eligibility or publish locals. Declared types and
// explicitly named const dependencies may be completed through their normal
// queries; ordinary let values never become constant facts through this walk.
// The normal checking walk validates every expression afterwards.
auto Checker::hint(Id node, const std::unordered_map<std::string, Type> &locals,
                   bool applyDefaults, bool locateOnly) -> Type {
  const auto &value = Tree.Nodes[node];
  const auto child = [&](Id id) {
    return hint(id, locals, applyDefaults, locateOnly);
  };
  switch (value.Form) {
  case Parse::Kind::Group:
    return child(value.Left);
  case Parse::Kind::Unary: {
    const auto operand = child(value.Left);
    if (operand == Type::Never) {
      return Type::Never;
    }
    return value.Operator == Lex::TokenKind::Bang ? Type::Bool : operand;
  }
  case Parse::Kind::Integer:
  case Parse::Kind::Float:
    if (value.SuffixLength != 0) {
      const auto text = Source.slice(value.Text);
      return Types::Lookup(text.substr(text.size() - value.SuffixLength))
          .value_or(Type::Invalid);
    }
    if (applyDefaults) {
      return value.Form == Parse::Kind::Float ? Type::F64 : Type::I32;
    }
    return Type::Invalid;
  case Parse::Kind::Boolean:
    return Type::Bool;
  case Parse::Kind::Unit:
    return Type::Unit;
  case Parse::Kind::Tuple: {
    std::vector<Type> elements;
    for (const auto item : value.Statements) {
      const auto element = child(item);
      if (element == Type::Never) {
        return Type::Never;
      }
      if (element == Type::Invalid) {
        return Type::Invalid;
      }
      elements.push_back(element);
    }
    return TypeBuilder.tuple(std::move(elements));
  }
  case Parse::Kind::Array: {
    auto element = Type::Invalid;
    for (const auto item : value.Statements) {
      const auto known = hint(item, locals, false);
      if (known == Type::Never) {
        return Type::Never;
      }
      if (element == Type::Invalid && known != Type::Invalid) {
        element = known;
      }
    }
    if (element == Type::Invalid && applyDefaults &&
        !value.Statements.empty()) {
      element = child(value.Statements[0]);
    }
    return element == Type::Invalid
               ? Type::Invalid
               : TypeBuilder.array(element, value.Statements.size());
  }
  case Parse::Kind::Name: {
    const auto spelling = name(value.Text);
    if (const auto found = locals.find(spelling); found != locals.end()) {
      return found->second;
    }
    if (const auto local = lookup(spelling)) {
      return Result.Functions[CurrentFunction].Locals[*local].ValueType;
    }
    if (const auto found = Constants.find(spelling); found != Constants.end()) {
      const auto value = constant(found->second);
      return value ? Target::SourceType(value->type()) : Type::Invalid;
    }
    return Type::Invalid;
  }
  case Parse::Kind::Convert:
    if (hint(value.Left, locals, false) == Type::Never) {
      return Type::Never;
    }
    return type(value.Annotation, Type::Invalid);
  case Parse::Kind::Construct:
    for (const auto field : value.Statements) {
      if (hint(field, locals, false) == Type::Never) {
        return Type::Never;
      }
    }
    if (const auto found = TypeNames.find(name(value.Text));
        found != TypeNames.end()) {
      return found->second;
    }
    if (const auto found = Aliases.find(name(value.Text));
        found != Aliases.end()) {
      return resolveAlias(found->second);
    }
    return Type::Invalid;
  case Parse::Kind::Copy:
  case Parse::Kind::Move:
    return child(value.Left);
  case Parse::Kind::Project:
  case Parse::Kind::Index: {
    const auto base = child(value.Left);
    if (base == Type::Never ||
        (!locateOnly && value.Form == Parse::Kind::Index &&
         hint(value.Right, locals, false) == Type::Never)) {
      return Type::Never;
    }
    if (base.owner() == nullptr) {
      return Type::Invalid;
    }
    const auto &record = TypeBuilder.get(base);
    if (record.Form == Types::Kind::Array) {
      if (value.Form == Parse::Kind::Index) {
        return record.Element;
      }
      return name(value.Member) == "len" ? Type::USize : Type::Invalid;
    }
    if (value.Form == Parse::Kind::Project &&
        (record.Form == Types::Kind::Struct ||
         record.Form == Types::Kind::Tuple)) {
      const auto member = name(value.Member);
      for (const auto &field : record.Fields) {
        if (field.Name == member) {
          return field.Type;
        }
      }
    }
    return Type::Invalid;
  }
  case Parse::Kind::Binary: {
    const auto leftHint = hint(value.Left, locals, false);
    const auto rightHint = hint(value.Right, locals, false);
    const bool logical = value.Operator == Lex::TokenKind::And ||
                         value.Operator == Lex::TokenKind::Or;
    if (leftHint == Type::Never || (!logical && rightHint == Type::Never)) {
      return Type::Never;
    }
    if (logical && rightHint == Type::Never) {
      const auto &left = Tree.Nodes[root(value.Left)];
      if (left.Form == Parse::Kind::Boolean &&
          (value.Operator == Lex::TokenKind::And ? left.Boolean
                                                 : !left.Boolean)) {
        return Type::Never;
      }
    }
    if (operation(value.Operator) >= Numeric::Operation::Less) {
      return Type::Bool;
    }
    // Discover fixed types before considering defaults from either literal.
    auto left = leftHint;
    auto right = rightHint;
    if (value.Operator == Lex::TokenKind::ShiftLeft ||
        value.Operator == Lex::TokenKind::ShiftRight) {
      return left == Type::Invalid && applyDefaults ? child(value.Left) : left;
    }
    if (left != Type::Invalid && left != Type::Never) {
      return left;
    }
    if (right != Type::Invalid && right != Type::Never) {
      return right;
    }
    return applyDefaults ? child(value.Left) : Type::Invalid;
  }
  case Parse::Kind::Call: {
    for (const auto &argument : value.Arguments) {
      if (hint(argument.Expression, locals, false) == Type::Never) {
        return Type::Never;
      }
    }
    const auto callee = root(value.Left);
    if (Tree.Nodes[callee].Form != Parse::Kind::Name) {
      return Type::Invalid;
    }
    const auto spelling = name(Tree.Nodes[callee].Text);
    if (locals.contains(spelling) || lookup(spelling)) {
      return Type::Invalid;
    }
    if (const auto found = Functions.find(spelling); found != Functions.end()) {
      const auto result = Result.Functions[found->second].ReturnType;
      // Calls returning any uninhabited type cannot contribute a normal
      // result. This includes empty enums and structs containing '!'.
      return result != Type::Invalid && !TypeBuilder.inhabited(result)
                 ? Type::Never
                 : result;
    }
    if (spelling == "panic" || spelling == "abort") {
      return Type::Never;
    }
    return spelling == "assert" ? Type::Unit : Type::Invalid;
  }
  case Parse::Kind::Block: {
    auto nested = locals;
    for (const auto statement : value.Statements) {
      const auto &item = Tree.Nodes[statement];
      if (item.Form == Parse::Kind::Return || item.Form == Parse::Kind::Break ||
          item.Form == Parse::Kind::Continue) {
        return Type::Never;
      }
      if (item.Form == Parse::Kind::Bind) {
        auto initializerType = hint(item.Left, nested, true);
        if (initializerType == Type::Never) {
          return Type::Never;
        }
        if (item.Annotation.Present) {
          initializerType = type(item.Annotation, Type::Invalid);
        }
        hintBindings(item.Pattern, initializerType, nested);
        continue;
      }
      const auto expression = item.Form == Parse::Kind::Discard  ? item.Left
                              : item.Form == Parse::Kind::Assign ? item.Right
                                                                 : statement;
      if (hint(expression, nested, applyDefaults) == Type::Never) {
        return Type::Never;
      }
    }
    return value.Left == SemIR::None ? Type::Unit
                                     : hint(value.Left, nested, applyDefaults);
  }
  case Parse::Kind::If: {
    if (hint(value.Left, locals, false) == Type::Never) {
      return Type::Never;
    }
    const auto first = hint(value.Right, locals, false);
    if (first != Type::Invalid && first != Type::Never) {
      return first;
    }
    const auto second = value.Alternative == SemIR::None
                            ? Type::Unit
                            : hint(value.Alternative, locals, false);
    if (second != Type::Invalid && second != Type::Never) {
      return second;
    }
    if (first == Type::Never && second == Type::Never) {
      return Type::Never;
    }
    if (!applyDefaults) {
      return Type::Invalid;
    }
    return first == Type::Never && value.Alternative != SemIR::None
               ? child(value.Alternative)
               : child(value.Right);
  }
  default:
    return Type::Invalid;
  }
}
} // namespace Chtholly::Check
