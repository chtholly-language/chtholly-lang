// SPDX-License-Identifier: MIT OR Apache-2.0
#include "checker.h"

#include <algorithm>

namespace Chtholly::Check {
using Type = Target::Type;
using Id = SemIR::Id;
auto Checker::call(Id node, bool executable) -> Type {
  const auto &syntax = Tree.Nodes[node];
  auto &checked = Result.Nodes[node];
  checked.Form = SemIR::Kind::Call;
  const auto callee = root(syntax.Left);
  std::vector<SemIR::Parameter> parameters;
  if (Tree.Nodes[callee].Form != Parse::Kind::Name) {
    fail(node, "only direct function calls are supported");
  } else {
    const auto spelling = name(Tree.Nodes[callee].Text);
    if (lookup(spelling)) {
      fail(node, "nearest local binding is not callable");
    } else if (const auto found = Functions.find(spelling);
               found != Functions.end()) {
      checked.Entity = found->second;
      const auto &function = Result.Functions[found->second];
      parameters = function.Parameters;
      checked.ValueType = function.ReturnType;
    } else if (spelling == "panic" || spelling == "abort" ||
               spelling == "assert") {
      checked.Intrinsic = spelling == "panic"   ? SemIR::Builtin::Panic
                          : spelling == "abort" ? SemIR::Builtin::Abort
                                                : SemIR::Builtin::Assert;
      checked.ValueType = spelling == "assert" ? Type::Unit : Type::Never;
      if (spelling == "assert") {
        parameters.push_back({"", SemIR::None, Type::Bool});
      }
    } else {
      fail(node, "unknown function '" + spelling + "'");
    }
  }
  std::vector<bool> supplied(parameters.size(), false);
  bool namedSeen = false;
  bool completes = true;
  std::size_t position = 0;
  for (const auto &argument : syntax.Arguments) {
    Id slot = SemIR::None;
    if (argument.Named) {
      namedSeen = true;
      const auto label = name(argument.Label);
      for (Id i = 0; i < parameters.size(); ++i) {
        if (!parameters[i].Label.empty() && parameters[i].Label == label) {
          slot = i;
          break;
        }
      }
    } else if (namedSeen) {
      fail(node, "positional argument follows a named argument");
    } else if (position < parameters.size()) {
      slot = static_cast<Id>(position++);
    }
    if (slot == SemIR::None || supplied[slot]) {
      fail(node, "unknown, duplicate, or excess argument");
    } else {
      supplied[slot] = true;
    }
    expression(argument.Expression,
               slot == SemIR::None ? Type::Invalid : parameters[slot].ValueType,
               executable && completes);
    completes &= Result.Nodes[argument.Expression].Completes;
    checked.Arguments.push_back(argument.Expression);
    checked.Slots.push_back(slot);
  }
  if (std::find(supplied.begin(), supplied.end(), false) != supplied.end()) {
    fail(node, "missing required argument");
  }
  checked.Completes = completes && checked.ValueType != Type::Never;
  return checked.ValueType;
}
auto Checker::block(Id node, Type context, bool executable,
                    bool newScope) -> Type {
  const auto &syntax = Tree.Nodes[node];
  auto &checked = Result.Nodes[node];
  checked.Form = SemIR::Kind::Block;
  checked.Executable = executable;
  checked.Statements = syntax.Statements;
  checked.Left = syntax.Left;
  if (newScope) {
    Scopes.emplace_back();
  }
  bool completes = true;
  bool constantStatements = true;
  for (const auto item : syntax.Statements) {
    statement(item, executable && completes);
    completes &= Result.Nodes[item].Completes;
    constantStatements &= Result.Nodes[item].Constant.has_value();
  }
  if (syntax.Left != SemIR::None) {
    checked.ValueType =
        expression(syntax.Left, completes ? context : Type::Invalid,
                   executable && completes);
    completes &= Result.Nodes[syntax.Left].Completes;
    if (constantStatements) {
      checked.Constant = Result.Nodes[syntax.Left].Constant;
    }
  } else {
    checked.ValueType = Type::Unit;
    if (constantStatements) {
      checked.Constant = Numeric::Value::Unit();
    }
  }
  checked.Completes = completes;
  if (!completes) {
    checked.ValueType = Type::Never;
    checked.Constant.reset();
  }
  if (newScope) {
    Scopes.pop_back();
  }
  expect(node, context);
  return checked.ValueType;
}
auto Checker::branch(Id node, Type context, bool executable) -> Type {
  const auto &syntax = Tree.Nodes[node];
  auto &checked = Result.Nodes[node];
  checked.Form = SemIR::Kind::If;
  checked.Left = syntax.Left;
  checked.Right = syntax.Right;
  checked.Alternative = syntax.Alternative;
  expression(syntax.Left, Type::Bool, executable);
  const auto &condition = Result.Nodes[syntax.Left];
  std::optional<bool> selected;
  if (condition.Constant && condition.ValueType == Type::Bool) {
    selected = condition.Constant->boolean();
  }
  auto branchContext = syntax.Alternative == SemIR::None ? Type::Unit : context;
  if (branchContext == Type::Invalid) {
    branchContext = hint(node);
  }
  if (branchContext == Type::Never) {
    branchContext = Type::Invalid;
  }
  expression(syntax.Right, branchContext,
             executable && condition.Completes && (!selected || *selected));
  if (syntax.Alternative != SemIR::None) {
    expression(syntax.Alternative, branchContext,
               executable && condition.Completes && (!selected || !*selected));
  }
  const auto &thenValue = Result.Nodes[syntax.Right];
  const auto elseType = syntax.Alternative == SemIR::None
                            ? Type::Unit
                            : Result.Nodes[syntax.Alternative].ValueType;
  if (thenValue.ValueType != Type::Never && elseType != Type::Never &&
      thenValue.ValueType != elseType) {
    fail(node, "normal if branches must produce the same type");
  }
  checked.ValueType =
      thenValue.ValueType == Type::Never ? elseType : thenValue.ValueType;
  const auto elseCompletes = syntax.Alternative == SemIR::None ||
                             Result.Nodes[syntax.Alternative].Completes;
  checked.Completes =
      condition.Completes &&
      (selected ? (*selected ? thenValue.Completes : elseCompletes)
                : thenValue.Completes || elseCompletes);
  if (!checked.Completes) {
    checked.ValueType = Type::Never;
  }
  if (condition.Completes && selected && checked.Completes) {
    if (*selected) {
      checked.Constant = thenValue.Constant;
    } else if (syntax.Alternative == SemIR::None) {
      checked.Constant = Numeric::Value::Unit();
    } else {
      checked.Constant = Result.Nodes[syntax.Alternative].Constant;
    }
  }
  return checked.ValueType;
}
auto Checker::bind(Id node, bool executable) -> void {
  const auto &syntax = Tree.Nodes[node];
  auto &checked = Result.Nodes[node];
  checked.Form = SemIR::Kind::Bind;
  checked.Left = syntax.Left;
  checked.Alternative = syntax.Alternative;
  const auto annotation = type(syntax.Annotation, Type::Invalid);
  const auto oldRoot = RecoveryRoot;
  const auto operationRoot = root(syntax.Left);
  if (syntax.Alternative != SemIR::None) {
    RecoveryRoot = operationRoot;
  }
  auto valueType = expression(syntax.Left, annotation, executable);
  RecoveryRoot = oldRoot;
  const auto &value = Result.Nodes[syntax.Left];
  const auto &operationValue = Result.Nodes[operationRoot];
  bool completes = value.Completes;
  if (syntax.Alternative != SemIR::None) {
    const bool conversion = operationValue.Form == SemIR::Kind::Convert;
    const auto op = operationValue.Operation;
    const bool arithmetic = Target::IsInteger(operationValue.ValueType) &&
                            ((operationValue.Form == SemIR::Kind::Unary &&
                              op == Numeric::Operation::Negate) ||
                             (operationValue.Form == SemIR::Kind::Binary &&
                              (op == Numeric::Operation::Add ||
                               op == Numeric::Operation::Subtract ||
                               op == Numeric::Operation::Multiply ||
                               op == Numeric::Operation::Divide ||
                               op == Numeric::Operation::Remainder ||
                               op == Numeric::Operation::ShiftLeft ||
                               op == Numeric::Operation::ShiftRight)));
    if (!conversion && !arithmetic) {
      fail(node, "else requires a directly checked numeric root operation");
    }
    const bool knownSuccess = operationValue.Constant.has_value();
    expression(syntax.Alternative, conversion ? valueType : Type::Invalid,
               executable && value.Completes && !knownSuccess);
    const auto &alternative = Result.Nodes[syntax.Alternative];
    if (!conversion && alternative.Completes) {
      fail(node, "arithmetic recovery must leave the initializer");
    }
    if (operationValue.ConstantFailure != Numeric::Failure::None) {
      completes = alternative.Completes;
    }
    if (conversion && valueType == Type::Never) {
      valueType = alternative.ValueType;
    }
  }
  auto &function = Result.Functions[CurrentFunction];
  const auto spelling = name(syntax.Text);
  if (spelling != "_" && Scopes.back().contains(spelling)) {
    fail(node, "duplicate local name in this scope");
  }
  checked.Entity = static_cast<Id>(function.Locals.size());
  function.Locals.push_back(
      {spelling, annotation == Type::Invalid ? valueType : annotation,
       syntax.Mutable});
  // Static name scope starts after the initializer. No runtime slot is
  // published on a divergent path; unreachable source still resolves names.
  if (spelling != "_") {
    Scopes.back()[spelling] = checked.Entity;
  }
  checked.ValueType = Type::Unit;
  checked.Completes = completes;
}
auto Checker::statement(Id node, bool executable) -> void {
  const auto &syntax = Tree.Nodes[node];
  auto &checked = Result.Nodes[node];
  checked.Executable = executable;
  checked.ValueType = Type::Unit;
  switch (syntax.Form) {
  case Parse::Kind::Bind:
    bind(node, executable);
    return;
  case Parse::Kind::Return:
    checked.Form = SemIR::Kind::Return;
    checked.Left = syntax.Left;
    if (syntax.Left == SemIR::None) {
      if (Result.Functions[CurrentFunction].ReturnType != Type::Unit) {
        fail(node, "return requires a value of the declared return type");
      }
    } else {
      expression(syntax.Left, Result.Functions[CurrentFunction].ReturnType,
                 executable);
    }
    checked.Completes = false;
    return;
  case Parse::Kind::Assign: {
    checked.Form = SemIR::Kind::Assign;
    checked.Right = syntax.Right;
    const auto target = root(syntax.Left);
    Type targetType = Type::Invalid;
    if (Tree.Nodes[target].Form != Parse::Kind::Name) {
      fail(node, "assignment target must be a local position");
    } else if (const auto local = lookup(name(Tree.Nodes[target].Text))) {
      checked.Entity = *local;
      const auto &binding = Result.Functions[CurrentFunction].Locals[*local];
      targetType = binding.ValueType;
      if (!binding.Writable) {
        fail(node, "cannot assign to an immutable local position");
      }
    } else {
      fail(node, "unknown assignment target");
    }
    expression(syntax.Right, targetType, executable);
    checked.Completes = Result.Nodes[syntax.Right].Completes;
    return;
  }
  case Parse::Kind::Discard:
    checked.Form = SemIR::Kind::Discard;
    checked.Left = syntax.Left;
    expression(syntax.Left, Type::Invalid, executable);
    checked.Completes = Result.Nodes[syntax.Left].Completes;
    if (Result.Nodes[syntax.Left].Constant) {
      checked.Constant = Numeric::Value::Unit();
    }
    return;
  default:
    expression(node, Type::Unit, executable);
    return;
  }
}
} // namespace Chtholly::Check
