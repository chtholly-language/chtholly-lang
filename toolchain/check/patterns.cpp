// SPDX-License-Identifier: MIT OR Apache-2.0
#include "checker.h"
#include <algorithm>
#include <unordered_set>

namespace Chtholly::Check {
using Id = SemIR::Id;
using Type = Types::TypeId;
auto Checker::pattern(Parse::PatternId syntaxId, Type valueType, bool writable,
                      bool trial,
                      std::unordered_map<std::string, Id> &bindings) -> Id {
  const auto &syntax = Tree.Patterns.at(syntaxId);
  SemIR::Pattern result;
  result.ValueType = valueType;
  result.Location = Source.locate(syntax.Text.Begin);
  const auto invalid = [&](std::string message) {
    error(syntax.Text.Begin, std::move(message));
  };
  if (valueType == Type::Invalid) {
    invalid("cannot form a pattern for an invalid input type");
    const auto id = static_cast<Id>(Result.Patterns.size());
    Result.Patterns.push_back(std::move(result));
    return id;
  }
  if (syntax.Mutable && syntax.Form != Parse::PatternKind::Binding) {
    invalid("var inside a pattern must mark a leaf binding");
  }
  const bool fieldPattern = syntax.Form == Parse::PatternKind::Struct ||
                            syntax.Form == Parse::PatternKind::Variant;
  if (syntax.Rest && !fieldPattern) {
    invalid("rest is only valid inside a tuple or field pattern");
  }
  if (syntax.Form == Parse::PatternKind::Group) {
    return pattern(syntax.Children.at(0), valueType, writable, trial, bindings);
  }
  if (syntax.Form == Parse::PatternKind::Binding ||
      syntax.Form == Parse::PatternKind::Wildcard) {
    const auto spelling = name(syntax.Text);
    result.Form = SemIR::PatternKind::Wildcard;
    if (syntax.Form == Parse::PatternKind::Binding && spelling != "_") {
      result.Form = SemIR::PatternKind::Binding;
      const bool finalWritable = writable || syntax.Mutable;
      if (const auto existing = bindings.find(spelling);
          existing != bindings.end()) {
        result.Local = existing->second;
        const auto &local =
            Result.Functions[CurrentFunction].Locals[result.Local];
        if (local.ValueType != valueType || local.Writable != finalWritable) {
          invalid("or alternatives require identical binding types and final "
                  "permissions");
        }
      } else {
        auto &locals = Result.Functions[CurrentFunction].Locals;
        result.Local = static_cast<Id>(locals.size());
        locals.push_back({spelling, valueType, finalWritable, trial});
        bindings[spelling] = result.Local;
      }
    }
  } else if (syntax.Form == Parse::PatternKind::Or) {
    result.Form = SemIR::PatternKind::Or;
    const auto before = bindings;
    std::unordered_map<std::string, Id> expected;
    for (std::size_t i = 0; i < syntax.Children.size(); ++i) {
      auto alternative = i == 0 ? before : expected;
      const auto child =
          pattern(syntax.Children[i], valueType, writable, trial, alternative);
      result.Children.push_back(child);
      std::unordered_set<std::string> present;
      std::vector<Id> work{child};
      while (!work.empty()) {
        const auto &item = Result.Patterns[work.back()];
        work.pop_back();
        if (item.Form == SemIR::PatternKind::Binding) {
          present.insert(
              Result.Functions[CurrentFunction].Locals[item.Local].Name);
        }
        for (const auto nested : item.Children) {
          work.push_back(nested);
        }
      }
      if (i == 0) {
        expected = alternative;
      } else {
        std::unordered_set<std::string> wanted;
        for (const auto &[spelling, local] : expected) {
          if (!before.contains(spelling)) {
            wanted.insert(spelling);
          }
        }
        if (present != wanted) {
          invalid("or alternatives require the same binding names");
        }
      }
    }
    bindings = std::move(expected);
  } else if (syntax.Form == Parse::PatternKind::Integer) {
    result.Form = SemIR::PatternKind::Integer;
    if (!Types::IsInteger(valueType)) {
      invalid("integer pattern requires the same integer input type");
    }
    auto digits = Source.slice(syntax.Text);
    auto literalType = valueType;
    if (syntax.SuffixLength != 0) {
      literalType =
          Types::Lookup(digits.substr(digits.size() - syntax.SuffixLength))
              .value_or(Type::Invalid);
      digits.remove_suffix(syntax.SuffixLength);
      if (literalType != valueType) {
        invalid("pattern suffix must match the input type");
      }
    }
    if (syntax.Radix != 10) {
      digits.remove_prefix(2);
    }
    const auto formed =
        Numeric::Literal(digits, syntax.Radix, Target::ScalarOf(literalType),
                         syntax.Negative, Budget);
    result.Literal = formed.ValueResult;
    if (!formed) {
      invalid("invalid integer pattern: " +
              std::string(Numeric::FailureName(formed.Error)));
    }
  } else if (syntax.Form == Parse::PatternKind::Boolean) {
    result.Form = SemIR::PatternKind::Boolean;
    result.Literal = Numeric::Value::Boolean(syntax.Boolean);
    if (valueType != Type::Bool) {
      invalid("boolean pattern requires bool input");
    }
  } else if (syntax.Form == Parse::PatternKind::Unit) {
    result.Form = SemIR::PatternKind::Unit;
    if (valueType != Type::Unit) {
      invalid("unit pattern requires unit input");
    }
  } else {
    result.Form = SemIR::PatternKind::Product;
    std::vector<Types::Field> fields;
    if (syntax.Form == Parse::PatternKind::Tuple && valueType == Type::Unit) {
      // The empty structural tuple is the builtin unit identity. A tuple
      // rest can cover its zero fields without inventing an aggregate type.
    } else if (valueType.owner() == nullptr) {
      invalid("aggregate pattern requires an aggregate input");
    } else {
      const auto declaration = TypeBuilder.get(valueType);
      if (syntax.Form == Parse::PatternKind::Tuple) {
        if (declaration.Form != Types::Kind::Tuple) {
          invalid("tuple pattern requires tuple input");
        } else {
          fields = declaration.Fields;
        }
      } else {
        const auto spelling = name(syntax.Text);
        auto namedType = TypeNames.find(spelling);
        if (namedType == TypeNames.end() || namedType->second != valueType) {
          invalid("pattern names a different nominal type");
        }
        if (syntax.Form == Parse::PatternKind::Variant) {
          result.Form = SemIR::PatternKind::Variant;
          if (declaration.Form != Types::Kind::Enum) {
            invalid("variant pattern requires enum input");
          }
          const auto variantName = name(syntax.Variant);
          for (Id i = 0; i < declaration.Variants.size(); ++i) {
            if (declaration.Variants[i].Name == variantName) {
              result.Variant = i;
              fields = declaration.Variants[i].Fields;
            }
          }
          if (result.Variant == SemIR::None) {
            invalid("unknown pattern variant");
          }
        } else if (declaration.Form != Types::Kind::Struct) {
          invalid("field pattern requires struct input");
        } else {
          fields = declaration.Fields;
        }
      }
    }
    std::unordered_set<std::uint64_t> supplied;
    std::unordered_set<std::string> leafNames;
    std::size_t rests = 0;
    if (syntax.Form == Parse::PatternKind::Tuple) {
      for (const auto child : syntax.Children) {
        rests += Tree.Patterns[child].isTupleRest() ? 1U : 0U;
      }
    }
    if (syntax.Form == Parse::PatternKind::Tuple &&
        (rests > 1 || syntax.Children.size() - rests > fields.size())) {
      invalid("invalid tuple rest pattern or arity");
    }
    bool afterRest = false;
    for (std::size_t i = 0; i < syntax.Children.size(); ++i) {
      if (syntax.Form == Parse::PatternKind::Tuple &&
          Tree.Patterns[syntax.Children[i]].isTupleRest()) {
        afterRest = true;
        continue;
      }
      std::uint64_t field = i;
      if (syntax.Form == Parse::PatternKind::Tuple && afterRest) {
        field = fields.size() - (syntax.Children.size() - i);
      }
      if (syntax.Form != Parse::PatternKind::Tuple) {
        field = fields.size();
        const auto spelling = name(syntax.Fields[i]);
        for (std::size_t j = 0; j < fields.size(); ++j) {
          if (fields[j].Name == spelling) {
            field = j;
          }
        }
      }
      if (field >= fields.size() || !supplied.insert(field).second) {
        invalid("unknown, duplicate, or excess pattern field");
        continue;
      }
      const auto child = pattern(syntax.Children[i], fields[field].Type,
                                 writable, trial, bindings);
      result.Children.push_back(child);
      result.Fields.push_back(field);
      std::vector<Id> work{child};
      while (!work.empty()) {
        const auto &item = Result.Patterns[work.back()];
        work.pop_back();
        if (item.Form == SemIR::PatternKind::Binding &&
            !leafNames
                 .insert(
                     Result.Functions[CurrentFunction].Locals[item.Local].Name)
                 .second) {
          invalid("duplicate name in pattern");
        }
        if (item.Form == SemIR::PatternKind::Or) {
          if (!item.Children.empty()) {
            work.push_back(item.Children[0]);
          }
        } else {
          for (const auto nested : item.Children) {
            work.push_back(nested);
          }
        }
      }
    }
    if (!syntax.Rest && rests == 0 && supplied.size() != fields.size()) {
      invalid("pattern must name every field or use '..'");
    }
  }
  const auto id = static_cast<Id>(Result.Patterns.size());
  Result.Patterns.push_back(std::move(result));
  return id;
}
auto Checker::bindPattern(Id id, bool trial) -> void {
  std::vector<Id> work{id};
  std::unordered_set<Id> seen;
  while (!work.empty()) {
    const auto &item = Result.Patterns.at(work.back());
    work.pop_back();
    if (item.Form == SemIR::PatternKind::Binding &&
        seen.insert(item.Local).second) {
      auto &local = Result.Functions[CurrentFunction].Locals[item.Local];
      local.Trial = trial;
      if (Scopes.back().contains(local.Name)) {
        error(0, "duplicate binding name in scope");
      }
      Scopes.back()[local.Name] = item.Local;
    }
    if (item.Form == SemIR::PatternKind::Or) {
      if (!item.Children.empty()) {
        work.push_back(item.Children[0]);
      }
    } else {
      for (const auto child : item.Children) {
        work.push_back(child);
      }
    }
  }
}
auto Checker::match(Id id, Type context, bool executable) -> Type {
  const auto &syntax = Tree.Nodes[id];
  auto &record = Result.Nodes[id];
  record.Form = SemIR::Kind::Match;
  record.Left = syntax.Left;
  const auto input = expression(syntax.Left, Type::Invalid, executable);
  const bool entered = executable && Result.Nodes[syntax.Left].Completes;
  Type result = context;
  bool completes = false;
  std::vector<Id> covering;
  std::vector<std::unordered_map<std::string, Id>> armBindings;
  std::vector<Id> armPatterns;
  for (const auto &arm : syntax.Arms) {
    std::unordered_map<std::string, Id> bindings;
    armPatterns.push_back(pattern(arm.Pattern, input, false, true, bindings));
    armBindings.push_back(std::move(bindings));
  }
  if (Failed) {
    // Coverage consumes normalized, well-typed patterns. In particular, an
    // invalid integer literal has no Numeric value to compare. Preserve the
    // source diagnostic instead of passing a half-built record to the solver.
    record.ValueType = Type::Invalid;
    record.Completes = false;
    return record.ValueType;
  }
  if (result == Type::Invalid) {
    for (std::size_t i = 0; i < syntax.Arms.size(); ++i) {
      std::unordered_map<std::string, Type> types;
      for (const auto &[spelling, local] : armBindings[i]) {
        types[spelling] =
            Result.Functions[CurrentFunction].Locals[local].ValueType;
      }
      const auto known = hint(syntax.Arms[i].Body, types, false);
      if (known != Type::Invalid && known != Type::Never) {
        result = known;
        break;
      }
    }
  }
  for (std::size_t i = 0; i < syntax.Arms.size(); ++i) {
    const auto &arm = syntax.Arms[i];
    Scopes.emplace_back();
    const auto &bindings = armBindings[i];
    const auto checkedPattern = armPatterns[i];
    const bool reachable =
        input != Type::Invalid &&
        !coverage(input, covering, id, false, checkedPattern);
    bindPattern(checkedPattern, true);
    if (arm.Guard != SemIR::None) {
      expression(arm.Guard, Type::Bool, entered && reachable);
    }
    const bool bodyReachable =
        reachable && (arm.Guard == SemIR::None ||
                      (Result.Nodes[arm.Guard].Completes &&
                       (!Result.Nodes[arm.Guard].Constant ||
                        Result.Nodes[arm.Guard].Constant->boolean())));
    for (const auto &[spelling, local] : bindings) {
      Result.Functions[CurrentFunction].Locals[local].Trial = false;
    }
    const auto armType = expression(arm.Body, result, entered && bodyReachable);
    if (Result.Nodes[arm.Body].Completes && armType != Type::Never) {
      if (result == Type::Invalid) {
        result = armType;
      } else if (result != armType) {
        fail(id, "normal match arms must produce the same type");
      }
      completes |= bodyReachable;
    }
    record.Arms.push_back(
        {checkedPattern, arm.Guard, arm.Body, reachable, bodyReachable});
    if (arm.Guard == SemIR::None) {
      covering.push_back(checkedPattern);
    }
    Scopes.pop_back();
  }
  if (input != Type::Invalid) {
    coverage(input, covering, id);
  }
  record.Completes = Result.Nodes[syntax.Left].Completes && completes &&
                     input != Type::Invalid && TypeBuilder.inhabited(input);
  record.ValueType = record.Completes ? result : Type::Never;
  return record.ValueType;
}
} // namespace Chtholly::Check
