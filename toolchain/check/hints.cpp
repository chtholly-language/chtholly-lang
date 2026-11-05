// SPDX-License-Identifier: MIT OR Apache-2.0
#include "checker.h"

namespace Chtholly::Check {
// This walk only supplies already known types and binding-local defaults.
// It cannot grant eligibility, form constants, publish locals, or emit errors.
// The normal checking walk always validates every node afterwards.
auto Checker::hint(Id node, const std::unordered_map<std::string, Type> &locals,
                   bool applyDefaults) const -> Type {
  const auto &value = Tree.Nodes[node];
  const auto child = [&](Id id) { return hint(id, locals, applyDefaults); };
  switch (value.Form) {
  case Parse::Kind::Group:
    return child(value.Left);
  case Parse::Kind::Unary:
    return value.Operator == Lex::TokenKind::Bang ? Type::Bool
                                                  : child(value.Left);
  case Parse::Kind::Integer:
  case Parse::Kind::Float:
    if (value.SuffixLength != 0) {
      const auto text = Source.slice(value.Text);
      return Target::Lookup(text.substr(text.size() - value.SuffixLength))
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
  case Parse::Kind::Name: {
    const auto spelling = name(value.Text);
    if (const auto found = locals.find(spelling); found != locals.end()) {
      return found->second;
    }
    if (const auto local = lookup(spelling)) {
      return Result.Functions[CurrentFunction].Locals[*local].ValueType;
    }
    return Type::Invalid;
  }
  case Parse::Kind::Convert:
    return value.Annotation.Unit ? Type::Unit
                                 : Target::Lookup(name(value.Annotation.Text))
                                       .value_or(Type::Invalid);
  case Parse::Kind::Binary: {
    if (operation(value.Operator) >= Numeric::Operation::Less) {
      return Type::Bool;
    }
    // Discover fixed types before considering defaults from either literal.
    auto left = hint(value.Left, locals, false);
    auto right = hint(value.Right, locals, false);
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
    const auto callee = root(value.Left);
    if (Tree.Nodes[callee].Form != Parse::Kind::Name) {
      return Type::Invalid;
    }
    const auto spelling = name(Tree.Nodes[callee].Text);
    if (locals.contains(spelling) || lookup(spelling)) {
      return Type::Invalid;
    }
    if (const auto found = Functions.find(spelling); found != Functions.end()) {
      return Result.Functions[found->second].ReturnType;
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
      if (item.Form == Parse::Kind::Return) {
        return Type::Never;
      }
      if (item.Form == Parse::Kind::Bind) {
        auto initializerType = hint(item.Left, nested, true);
        if (initializerType == Type::Never) {
          return Type::Never;
        }
        if (item.Annotation.Present) {
          initializerType = item.Annotation.Unit
                                ? Type::Unit
                                : Target::Lookup(name(item.Annotation.Text))
                                      .value_or(Type::Invalid);
        }
        const auto spelling = name(item.Text);
        if (spelling != "_") {
          nested[spelling] = initializerType;
        }
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
