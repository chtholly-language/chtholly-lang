// SPDX-License-Identifier: MIT OR Apache-2.0
#include "checker.h"
#include <algorithm>

namespace Chtholly::Check {
using Type = Types::TypeId;
using Id = SemIR::Id;

// A hint is not a second checking pass. Each block has a lexical environment,
// including names whose types are unknown. They still shadow outer bindings.
auto Checker::hintBreaks(Id id, const std::string &label, unsigned nestedLoops,
                         std::unordered_map<std::string, Type> locals) -> Type {
  if (id == SemIR::None) {
    return Type::Invalid;
  }
  const auto &node = Tree.Nodes[id];
  const auto scan = [&](Id child) {
    return hintBreaks(child, label, nestedLoops, locals);
  };
  const auto useful = [](Type type) {
    return type != Type::Invalid && type != Type::Never;
  };
  if (node.Form == Parse::Kind::Break &&
      ((node.Label.Length == 0 && nestedLoops == 0) ||
       (!label.empty() && name(node.Label) == label))) {
    // An operand can leave the loop before the enclosing break is reached.
    // Such a nested jump contributes its own value, not the dead tail value.
    const auto nested = scan(node.Left);
    if (useful(nested)) {
      return nested;
    }
    return node.Left == SemIR::None ? Type::Unit
                                    : hint(node.Left, locals, false);
  }
  if (node.Form == Parse::Kind::Block) {
    for (const auto statement : node.Statements) {
      const auto found = scan(statement);
      if (useful(found)) {
        return found;
      }
      const auto &item = Tree.Nodes[statement];
      if (item.Form == Parse::Kind::Bind) {
        const auto input = item.Annotation.Present
                               ? type(item.Annotation, Type::Invalid)
                               : hint(item.Left, locals, true);
        hintBindings(item.Pattern, input, locals);
      }
    }
    return scan(node.Left);
  }
  if (node.Form == Parse::Kind::Match) {
    const auto inputBreak = scan(node.Left);
    if (useful(inputBreak)) {
      return inputBreak;
    }
    const auto input = hint(node.Left, locals, true);
    for (const auto &arm : node.Arms) {
      auto armLocals = locals;
      hintBindings(arm.Pattern, input, armLocals);
      for (const auto child : {arm.Guard, arm.Body}) {
        const auto found = hintBreaks(child, label, nestedLoops, armLocals);
        if (useful(found)) {
          return found;
        }
      }
    }
    return Type::Invalid;
  }
  const bool nestedLoop =
      node.Form == Parse::Kind::Loop || node.Form == Parse::Kind::While;
  const auto depth = nestedLoops + (nestedLoop ? 1U : 0U);
  for (const auto child : {node.Left, node.Right, node.Alternative}) {
    const auto found = hintBreaks(child, label, depth, locals);
    if (useful(found)) {
      return found;
    }
  }
  for (const auto child : node.Statements) {
    const auto found = hintBreaks(child, label, depth, locals);
    if (useful(found)) {
      return found;
    }
  }
  for (const auto &argument : node.Arguments) {
    const auto found = hintBreaks(argument.Expression, label, depth, locals);
    if (useful(found)) {
      return found;
    }
  }
  return Type::Invalid;
}

auto Checker::loop(Id id, Type context, bool executable) -> Type {
  const auto &syntax = Tree.Nodes[id];
  auto &record = Result.Nodes[id];
  const bool isWhile = syntax.Form == Parse::Kind::While;
  const auto label = name(syntax.Label);
  if (!label.empty() &&
      std::any_of(Loops.begin(), Loops.end(), [&](const LoopContext &loop) {
        return loop.Label == label;
      })) {
    fail(id, "duplicate active loop label");
  }
  if (!isWhile && context == Type::Invalid) {
    const auto known = hintBreaks(syntax.Right, label, 0, {});
    if (known != Type::Never) {
      context = known;
    }
  }
  Loops.push_back({id, label, isWhile, isWhile ? Type::Unit : context, false});
  record.Form = isWhile ? SemIR::Kind::While : SemIR::Kind::Loop;
  record.Left = syntax.Left;
  record.Right = syntax.Right;
  bool bodyExecutable = executable;
  if (isWhile) {
    expression(syntax.Left, Type::Bool, executable);
    const auto &condition = Result.Nodes[syntax.Left];
    bodyExecutable &= condition.Completes &&
                      (!condition.Constant || condition.Constant->boolean());
  }
  block(syntax.Right, Type::Unit, bodyExecutable);
  const auto loop = Loops.back();
  Loops.pop_back();
  record.Completes = isWhile
                         ? Result.Nodes[syntax.Left].Completes || loop.HasBreak
                         : loop.HasBreak;
  record.ValueType = isWhile         ? Type::Unit
                     : loop.HasBreak ? loop.Result
                                     : Type::Never;
  return record.ValueType;
}
} // namespace Chtholly::Check
