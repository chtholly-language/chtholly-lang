// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/IR/ir.h>

#include <algorithm>
#include <iterator>
#include <set>
#include <stdexcept>

namespace Chtholly::IR {
namespace {
using O = Numeric::Operation;
auto Require(bool condition, const char *reason) -> void {
  if (!condition) {
    throw std::logic_error(reason);
  }
}
auto OperandTypes(O op, Type left, Type right, Type result,
                  bool checked) -> bool {
  const bool compare = op >= O::Less && op <= O::NotEqual;
  if (compare) {
    return !checked && left == right && result == Type::Bool &&
           (Target::IsNumeric(left) ||
            ((op == O::Equal || op == O::NotEqual) && left == Type::Bool));
  }
  if (op == O::ShiftLeft || op == O::ShiftRight) {
    return checked && Target::IsInteger(left) && Target::IsInteger(right) &&
           result == left;
  }
  if (left != right || result != left) {
    return false;
  }
  if (op == O::BitAnd || op == O::BitOr || op == O::BitXor) {
    return !checked && Target::IsInteger(left);
  }
  if (op == O::Remainder) {
    return checked && Target::IsInteger(left);
  }
  if (op == O::Add || op == O::Subtract || op == O::Multiply ||
      op == O::Divide) {
    return Target::IsInteger(left) ? checked
                                   : !checked && Target::IsFloat(left);
  }
  return false;
}
auto UnaryTypes(O op, Type source, Type result, bool checked) -> bool {
  if (source != result) {
    return false;
  }
  if (op == O::Not) {
    return !checked && source == Type::Bool;
  }
  if (op == O::Complement) {
    return !checked && Target::IsInteger(source);
  }
  if (op == O::Plus) {
    return !checked && Target::IsNumeric(source);
  }
  if (op == O::Negate) {
    return Target::IsInteger(source) ? checked
                                     : !checked && Target::IsFloat(source);
  }
  return false;
}
auto VerifyFunction(const Draft &program, const Function &function) -> void {
  const auto blocks = function.Blocks.size();
  bool inhabited = true;
  for (const auto local : function.Locals) {
    Require(local >= Type::I8 && local <= Type::Never, "invalid local type");
  }
  std::set<Id> parameterLocals;
  for (const auto &parameter : function.Parameters) {
    Require(parameter.Local < function.Locals.size(),
            "parameter local ID out of range");
    Require(function.Locals[parameter.Local] == parameter.ValueType,
            "parameter/local type mismatch");
    Require(parameter.ValueType >= Type::I8 &&
                parameter.ValueType <= Type::Never,
            "invalid parameter type");
    Require(parameterLocals.insert(parameter.Local).second,
            "parameters share a local slot");
    inhabited &= parameter.ValueType != Type::Never;
  }
  Require(inhabited == function.HasNormalEntry,
          "normal entry proof disagrees with parameter types");
  Require(function.ReturnType >= Type::I8 && function.ReturnType <= Type::Never,
          "invalid return type");
  if (!inhabited) {
    Require(blocks == 0 && function.Values.empty(),
            "uninhabited function has executable values or blocks");
    return;
  }
  Require(blocks != 0, "function has no entry block");
  Require(function.Blocks[0].Parameters.size() == function.Parameters.size(),
          "entry parameter count mismatch");
  const auto type = [&](Id value) -> Type {
    Require(value < function.Values.size(),
            "value ID out of range or foreign owner");
    const auto result = function.Values[value];
    Require(Target::IsInhabited(result),
            "value has no constructible source type");
    return result;
  };
  for (std::size_t i = 0; i < function.Parameters.size(); ++i) {
    Require(type(function.Blocks[0].Parameters[i]) ==
                function.Parameters[i].ValueType,
            "entry parameter type mismatch");
  }
  std::vector<std::vector<Id>> predecessors(blocks);
  std::vector<std::pair<Id, std::size_t>> definitions(function.Values.size(),
                                                      {None, 0});
  const auto define = [&](Id value, Id block, std::size_t order) {
    type(value);
    Require(definitions[value].first == None, "value has multiple definitions");
    definitions[value] = {block, order};
  };
  for (Id b = 0; b < blocks; ++b) {
    const auto &block = function.Blocks[b];
    for (const auto value : block.Parameters) {
      define(value, b, 0);
    }
    const auto edge = [&](const Edge &destination,
                          std::optional<Type> implicit = std::nullopt) {
      Require(destination.Target < blocks && destination.Target != 0,
              "invalid branch target");
      predecessors[destination.Target].push_back(b);
      const auto &parameters = function.Blocks[destination.Target].Parameters;
      if (implicit) {
        Require(destination.Arguments.empty() && parameters.size() == 1 &&
                    type(parameters[0]) == *implicit,
                "checked edge result parameter mismatch");
      } else {
        Require(parameters.size() == destination.Arguments.size(),
                "edge argument count mismatch");
        for (std::size_t i = 0; i < parameters.size(); ++i) {
          Require(type(parameters[i]) == type(destination.Arguments[i]),
                  "edge argument type mismatch");
        }
      }
    };
    for (std::size_t i = 0; i < block.Instructions.size(); ++i) {
      const auto &instruction = block.Instructions[i];
      if (instruction.Result != None) {
        define(instruction.Result, b, i + 1);
      }
      const auto count = instruction.Operands.size();
      for (const auto value : instruction.Operands) {
        type(value);
      }
      switch (instruction.Code) {
      case Opcode::Constant:
        Require(count == 0 && instruction.Constant &&
                    instruction.Result != None &&
                    instruction.Constant->type() == type(instruction.Result),
                "invalid constant");
        break;
      case Opcode::Load:
      case Opcode::Store:
        Require(instruction.Entity < function.Locals.size(),
                "local ID out of range");
        if (instruction.Code == Opcode::Load) {
          Require(count == 0 && type(instruction.Result) ==
                                    function.Locals[instruction.Entity],
                  "load type mismatch");
        } else {
          Require(count == 1 && instruction.Result == None &&
                      type(instruction.Operands[0]) ==
                          function.Locals[instruction.Entity],
                  "store type mismatch");
        }
        break;
      case Opcode::Unary:
        Require(count == 1 && UnaryTypes(instruction.Operation,
                                         type(instruction.Operands[0]),
                                         type(instruction.Result), false),
                "invalid unary instruction");
        break;
      case Opcode::Binary:
        Require(count == 2 && OperandTypes(instruction.Operation,
                                           type(instruction.Operands[0]),
                                           type(instruction.Operands[1]),
                                           type(instruction.Result), false),
                "invalid binary instruction");
        break;
      case Opcode::Call: {
        Require(instruction.Entity < program.Functions.size(),
                "callee ID out of range");
        const auto &callee = program.Functions[instruction.Entity];
        Require(callee.HasNormalEntry && count == callee.Parameters.size(),
                "call cannot form all parameters");
        for (std::size_t p = 0; p < count; ++p) {
          Require(type(instruction.Operands[p]) ==
                      callee.Parameters[p].ValueType,
                  "call argument type mismatch");
        }
        if (callee.ReturnType == Type::Never) {
          Require(instruction.Result == None &&
                      i + 1 == block.Instructions.size() &&
                      block.End.Kind == Exit::Unreachable,
                  "noreturn call has a normal successor");
        } else {
          Require(type(instruction.Result) == callee.ReturnType,
                  "call result type mismatch");
        }
        break;
      }
      default:
        Require(false, "unknown instruction opcode");
        break;
      }
    }
    const auto &end = block.End;
    for (const auto value : end.Operands) {
      type(value);
    }
    switch (end.Kind) {
    case Exit::Missing:
      Require(false, "block has no terminator");
      break;
    case Exit::Jump:
      Require(end.Operands.empty(), "jump has extra operands");
      edge(end.First);
      break;
    case Exit::Branch:
      Require(end.Operands.size() == 1 && type(end.Operands[0]) == Type::Bool,
              "branch condition is not bool");
      edge(end.First);
      edge(end.Second);
      break;
    case Exit::Checked:
      Require(end.Operands.size() == 1 || end.Operands.size() == 2,
              "invalid checked operand count");
      if (end.Conversion) {
        Require(end.Operands.size() == 1 &&
                    Target::IsNumeric(type(end.Operands[0])) &&
                    Target::IsNumeric(end.ResultType),
                "invalid numeric conversion");
      } else if (end.Operands.size() == 1) {
        Require(UnaryTypes(end.Operation, type(end.Operands[0]), end.ResultType,
                           true),
                "invalid checked unary operation");
      } else {
        Require(OperandTypes(end.Operation, type(end.Operands[0]),
                             type(end.Operands[1]), end.ResultType, true),
                "invalid checked binary operation");
      }
      edge(end.First, end.ResultType);
      edge(end.Second, Type::U32);
      break;
    case Exit::Return:
      Require(end.Operands.size() == 1 && function.ReturnType != Type::Never &&
                  type(end.Operands[0]) == function.ReturnType,
              "return type mismatch");
      break;
    case Exit::Fail:
      Require(end.Operands.size() == 1 && type(end.Operands[0]) == Type::U32 &&
                  end.Location.Line != 0 && end.Location.Column != 0,
              "invalid failure record");
      break;
    case Exit::Abort:
    case Exit::Unreachable:
      Require(end.Operands.empty(), "terminal operation has operands");
      break;
    default:
      Require(false, "unknown terminator kind");
      break;
    }
  }
  for (const auto &definition : definitions) {
    Require(definition.first != None, "value has no definition");
  }
  // Dominance is calculated independently of the producer. Even structurally
  // unreachable records are type-checked; only reachable paths carry dataflow.
  std::vector<bool> reachable(blocks, false);
  reachable[0] = true;
  bool changed = true;
  while (changed) {
    changed = false;
    for (Id b = 1; b < blocks; ++b) {
      if (!reachable[b] &&
          std::any_of(predecessors[b].begin(), predecessors[b].end(),
                      [&](Id p) { return reachable[p]; })) {
        reachable[b] = true;
        changed = true;
      }
    }
  }
  std::set<Id> all;
  for (Id b = 0; b < blocks; ++b) {
    if (reachable[b]) {
      all.insert(b);
    }
  }
  std::vector<std::set<Id>> dominates(blocks, all);
  dominates[0] = {0};
  changed = true;
  while (changed) {
    changed = false;
    for (Id b = 1; b < blocks; ++b) {
      if (!reachable[b]) {
        continue;
      }
      auto intersection = all;
      for (const auto predecessor : predecessors[b]) {
        if (!reachable[predecessor]) {
          continue;
        }
        std::set<Id> next;
        std::set_intersection(intersection.begin(), intersection.end(),
                              dominates[predecessor].begin(),
                              dominates[predecessor].end(),
                              std::inserter(next, next.end()));
        intersection = std::move(next);
      }
      intersection.insert(b);
      if (intersection != dominates[b]) {
        dominates[b] = std::move(intersection);
        changed = true;
      }
    }
  }
  for (Id b = 0; b < blocks; ++b) {
    const auto use = [&](Id value, std::size_t order) {
      const auto [owner, definitionOrder] = definitions.at(value);
      Require(owner == b ? definitionOrder < order
                         : !reachable[b] || dominates[b].contains(owner),
              "use is not dominated by its definition");
    };
    const auto &block = function.Blocks[b];
    for (std::size_t i = 0; i < block.Instructions.size(); ++i) {
      for (const auto operand : block.Instructions[i].Operands) {
        use(operand, i + 1);
      }
    }
    const auto order = block.Instructions.size() + 1;
    for (const auto operand : block.End.Operands) {
      use(operand, order);
    }
    for (const auto operand : block.End.First.Arguments) {
      use(operand, order);
    }
    for (const auto operand : block.End.Second.Arguments) {
      use(operand, order);
    }
  }
  std::set<Id> everyLocal;
  for (Id local = 0; local < function.Locals.size(); ++local) {
    everyLocal.insert(local);
  }
  std::vector<std::set<Id>> initializedIn(blocks, everyLocal);
  std::vector<std::set<Id>> initializedOut(blocks, everyLocal);
  initializedIn[0].clear();
  changed = true;
  while (changed) {
    changed = false;
    for (Id b = 0; b < blocks; ++b) {
      if (!reachable[b]) {
        continue;
      }
      std::set<Id> incoming;
      if (b != 0) {
        incoming = everyLocal;
        for (const auto predecessor : predecessors[b]) {
          if (!reachable[predecessor]) {
            continue;
          }
          std::set<Id> next;
          std::set_intersection(incoming.begin(), incoming.end(),
                                initializedOut[predecessor].begin(),
                                initializedOut[predecessor].end(),
                                std::inserter(next, next.end()));
          incoming = std::move(next);
        }
      }
      auto outgoing = incoming;
      for (const auto &instruction : function.Blocks[b].Instructions) {
        if (instruction.Code == Opcode::Store) {
          outgoing.insert(instruction.Entity);
        }
      }
      if (incoming != initializedIn[b] || outgoing != initializedOut[b]) {
        initializedIn[b] = std::move(incoming);
        initializedOut[b] = std::move(outgoing);
        changed = true;
      }
    }
  }
  for (Id b = 0; b < blocks; ++b) {
    if (!reachable[b]) {
      continue;
    }
    auto initialized = initializedIn[b];
    for (const auto &instruction : function.Blocks[b].Instructions) {
      if (instruction.Code == Opcode::Load) {
        Require(initialized.contains(instruction.Entity),
                "read of an uninitialized local slot");
      }
      if (instruction.Code == Opcode::Store) {
        initialized.insert(instruction.Entity);
      }
    }
  }
}
} // namespace
auto Verify(Draft draft,
            Diagnostic::Diagnostics &diagnostics) -> std::optional<Program> {
  try {
    Require(draft.Target.valid(),
            "unsupported or inconsistent target description");
    // Do not retain a caller-owned string_view even when its spelling matches
    // the supported target. The verified program uses the canonical owner.
    draft.Target = Target::WindowsX64;
    Require(draft.Entry < draft.Functions.size(),
            "entry function ID out of range");
    const auto &entry = draft.Functions[draft.Entry];
    Require(entry.Name == "main" && entry.ReturnType == Type::I32 &&
                entry.Parameters.empty() && entry.HasNormalEntry,
            "invalid native entry signature");
    for (const auto &function : draft.Functions) {
      VerifyFunction(draft, function);
    }
    return Program(std::move(draft));
  } catch (const std::logic_error &error) {
    diagnostics.error("IR verification", error.what());
    return std::nullopt;
  }
}
} // namespace Chtholly::IR
