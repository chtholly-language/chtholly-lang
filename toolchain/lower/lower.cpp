// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/Lower/lower.h>

#include <stdexcept>

namespace Chtholly::Lower {
namespace {
using Id = IR::Id;
using Type = Target::Type;
using O = Numeric::Operation;
class Builder {
public:
  explicit Builder(const SemIR::Storage &source) : Source(source) {
    Result.Target = source.Target;
    Result.SourceName = source.SourceName;
    Result.Entry = source.Entry;
    for (const auto &function : source.Functions) {
      IR::Function lowered;
      lowered.Name = function.Name;
      lowered.ReturnType = function.ReturnType;
      lowered.HasNormalEntry = function.HasNormalEntry;
      for (const auto &parameter : function.Parameters) {
        lowered.Parameters.push_back({parameter.ValueType, parameter.Local});
      }
      for (const auto &local : function.Locals) {
        lowered.Locals.push_back(local.ValueType);
      }
      Result.Functions.push_back(std::move(lowered));
    }
  }
  auto run() -> IR::Draft {
    for (Id i = 0; i < Result.Functions.size(); ++i) {
      Function = &Result.Functions[i];
      if (!Function->HasNormalEntry) {
        continue;
      }
      Current = makeBlock();
      for (const auto &parameter : Function->Parameters) {
        const auto value = makeValue(parameter.ValueType);
        Function->Blocks[Current].Parameters.push_back(value);
        store(parameter.Local, value);
      }
      expression(Source.Functions[i].Body);
      if (Current != IR::None) {
        IR::Terminator end;
        end.Kind = IR::Exit::Return;
        end.Operands = {constant(Numeric::Value::Unit())};
        terminate(std::move(end));
      }
    }
    return std::move(Result);
  }

private:
  auto makeValue(Type type) -> Id {
    const auto id = static_cast<Id>(Function->Values.size());
    Function->Values.push_back(type);
    return id;
  }
  auto makeBlock(Type parameter = Type::Invalid) -> Id {
    const auto id = static_cast<Id>(Function->Blocks.size());
    Function->Blocks.emplace_back();
    if (parameter != Type::Invalid) {
      Function->Blocks[id].Parameters.push_back(makeValue(parameter));
    }
    return id;
  }
  auto parameter(Id block) const -> Id {
    return Function->Blocks[block].Parameters.at(0);
  }
  auto emit(IR::Instruction instruction, Type type) -> Id {
    if (Current == IR::None) {
      return IR::None;
    }
    if (type != Type::Invalid && type != Type::Never) {
      instruction.Result = makeValue(type);
    }
    const auto result = instruction.Result;
    Function->Blocks[Current].Instructions.push_back(std::move(instruction));
    return result;
  }
  auto constant(Numeric::Value value) -> Id {
    IR::Instruction instruction;
    instruction.Code = IR::Opcode::Constant;
    const auto type = value.type();
    instruction.Constant = std::move(value);
    return emit(std::move(instruction), type);
  }
  auto store(Id local, Id value) -> void {
    IR::Instruction instruction;
    instruction.Code = IR::Opcode::Store;
    instruction.Entity = local;
    instruction.Operands = {value};
    emit(std::move(instruction), Type::Invalid);
  }
  auto terminate(IR::Terminator end) -> void {
    if (Current == IR::None) {
      return;
    }
    Function->Blocks[Current].End = std::move(end);
    Current = IR::None;
  }
  auto jump(Id destination, std::vector<Id> arguments = {}) -> void {
    IR::Terminator end;
    end.Kind = IR::Exit::Jump;
    end.First = {destination, std::move(arguments)};
    terminate(std::move(end));
  }
  auto failureBlock(Id block, Source::Location location) -> void {
    Current = block;
    IR::Terminator end;
    end.Kind = IR::Exit::Fail;
    end.Operands = {parameter(block)};
    end.Location = location;
    terminate(std::move(end));
  }
  auto checked(const SemIR::Node &node, std::vector<Id> operands) -> Id {
    const auto success = makeBlock(node.ValueType);
    const auto failure = Recovery == IR::None ? makeBlock(Type::U32) : Recovery;
    IR::Terminator end;
    end.Kind = IR::Exit::Checked;
    end.Operands = std::move(operands);
    end.First.Target = success;
    end.Second.Target = failure;
    end.Operation = node.Operation;
    end.Conversion = node.Form == SemIR::Kind::Convert;
    end.ResultType = node.ValueType;
    end.Location = node.Location;
    terminate(std::move(end));
    if (Recovery == IR::None) {
      failureBlock(failure, node.Location);
    }
    Current = success;
    return parameter(success);
  }
  auto branch(const SemIR::Node &node) -> Id {
    const auto condition = expression(node.Left);
    if (Current == IR::None) {
      return IR::None;
    }
    if (Source.Nodes[node.Left].Constant) {
      if (Source.Nodes[node.Left].Constant->boolean()) {
        return expression(node.Right);
      }
      return node.Alternative == SemIR::None ? constant(Numeric::Value::Unit())
                                             : expression(node.Alternative);
    }
    const auto thenBlock = makeBlock();
    const auto elseBlock = makeBlock();
    const auto merge = node.Completes ? makeBlock(node.ValueType) : IR::None;
    IR::Terminator end;
    end.Kind = IR::Exit::Branch;
    end.Operands = {condition};
    end.First.Target = thenBlock;
    end.Second.Target = elseBlock;
    terminate(std::move(end));
    Current = thenBlock;
    const auto thenValue = expression(node.Right);
    if (Current != IR::None) {
      if (merge == IR::None) {
        IR::Terminator unreachable;
        unreachable.Kind = IR::Exit::Unreachable;
        terminate(unreachable);
      } else {
        jump(merge, {thenValue});
      }
    }
    Current = elseBlock;
    const auto elseValue = node.Alternative == SemIR::None
                               ? constant(Numeric::Value::Unit())
                               : expression(node.Alternative);
    if (Current != IR::None) {
      if (merge == IR::None) {
        IR::Terminator unreachable;
        unreachable.Kind = IR::Exit::Unreachable;
        terminate(unreachable);
      } else {
        jump(merge, {elseValue});
      }
    }
    Current = merge;
    return merge == IR::None ? IR::None : parameter(merge);
  }
  auto logical(const SemIR::Node &node) -> Id {
    const auto left = expression(node.Left);
    if (Current == IR::None) {
      return IR::None;
    }
    if (Source.Nodes[node.Left].Constant) {
      const auto condition = Source.Nodes[node.Left].Constant->boolean();
      if ((node.Operation == O::LogicalAnd && !condition) ||
          (node.Operation == O::LogicalOr && condition)) {
        return left;
      }
      return expression(node.Right);
    }
    const auto evaluate = makeBlock();
    const auto shortCircuit = makeBlock();
    const auto merge = makeBlock(Type::Bool);
    IR::Terminator end;
    end.Kind = IR::Exit::Branch;
    end.Operands = {left};
    end.First.Target =
        node.Operation == O::LogicalAnd ? evaluate : shortCircuit;
    end.Second.Target =
        node.Operation == O::LogicalAnd ? shortCircuit : evaluate;
    terminate(std::move(end));
    Current = shortCircuit;
    const auto shortValue =
        constant(Numeric::Value::Boolean(node.Operation == O::LogicalOr));
    jump(merge, {shortValue});
    Current = evaluate;
    const auto right = expression(node.Right);
    if (Current != IR::None) {
      jump(merge, {right});
    }
    Current = merge;
    return parameter(merge);
  }
  auto binding(const SemIR::Node &node) -> Id {
    if (node.Alternative == SemIR::None) {
      const auto value = expression(node.Left);
      if (Current != IR::None) {
        store(node.Entity, value);
      }
      return Current == IR::None ? IR::None : constant(Numeric::Value::Unit());
    }
    auto root = node.Left;
    while (Source.Nodes[root].Form == SemIR::Kind::Group) {
      root = Source.Nodes[root].Left;
    }
    if (!Source.Nodes[root].Completes) {
      // A divergent operand never reaches the checked root. There is neither
      // a success edge nor a recovery edge to manufacture in this case.
      return expression(node.Left);
    }
    if (Source.Nodes[root].Constant) {
      const auto value = expression(node.Left);
      if (Current != IR::None) {
        store(node.Entity, value);
      }
      return Current == IR::None ? IR::None : constant(Numeric::Value::Unit());
    }
    const auto recovery = makeBlock(Type::U32);
    const auto savedRoot = RecoveryNode;
    const auto savedRecovery = Recovery;
    RecoveryNode = root;
    Recovery = recovery;
    const auto value = expression(node.Left);
    RecoveryNode = savedRoot;
    Recovery = savedRecovery;
    const auto continuation =
        node.Completes ? makeBlock(Function->Locals[node.Entity]) : IR::None;
    if (Current != IR::None) {
      jump(continuation, {value});
    }
    Current = recovery;
    const auto replacement = expression(node.Alternative);
    if (Current != IR::None) {
      jump(continuation, {replacement});
    }
    Current = continuation;
    if (Current == IR::None) {
      return IR::None;
    }
    store(node.Entity, parameter(continuation));
    return constant(Numeric::Value::Unit());
  }
  auto call(const SemIR::Node &node) -> Id {
    std::vector<Id> arguments(node.Arguments.size(), IR::None);
    for (std::size_t i = 0; i < node.Arguments.size(); ++i) {
      const auto value = expression(node.Arguments[i]);
      if (Current == IR::None) {
        return IR::None;
      }
      arguments.at(node.Slots[i]) = value;
    }
    if (node.Intrinsic != SemIR::Builtin::None) {
      if (node.Intrinsic == SemIR::Builtin::Assert) {
        const auto success = makeBlock();
        const auto failure = makeBlock();
        IR::Terminator end;
        end.Kind = IR::Exit::Branch;
        end.Operands = {arguments.at(0)};
        end.First.Target = success;
        end.Second.Target = failure;
        terminate(std::move(end));
        Current = failure;
        IR::Terminator failed;
        failed.Kind = IR::Exit::Fail;
        failed.Location = node.Location;
        failed.Operands = {
            constant(Numeric::Value::FromBits(Type::U32, {21, 0}))};
        terminate(std::move(failed));
        Current = success;
        return constant(Numeric::Value::Unit());
      }
      IR::Terminator end;
      end.Kind = node.Intrinsic == SemIR::Builtin::Abort ? IR::Exit::Abort
                                                         : IR::Exit::Fail;
      end.Location = node.Location;
      if (end.Kind == IR::Exit::Fail) {
        end.Operands = {constant(Numeric::Value::FromBits(Type::U32, {20, 0}))};
      }
      terminate(std::move(end));
      return IR::None;
    }
    IR::Instruction instruction;
    instruction.Code = IR::Opcode::Call;
    instruction.Entity = node.Entity;
    instruction.Operands = std::move(arguments);
    const auto result = emit(std::move(instruction), node.ValueType);
    if (node.ValueType == Type::Never) {
      IR::Terminator unreachable;
      unreachable.Kind = IR::Exit::Unreachable;
      terminate(unreachable);
    }
    return result;
  }
  auto expression(Id id) -> Id {
    if (Current == IR::None) {
      return IR::None;
    }
    const auto &node = Source.Nodes.at(id);
    if (node.Constant && node.Completes) {
      return constant(*node.Constant);
    }
    if (node.Form != SemIR::Kind::Group &&
        Numeric::IsRecoverable(node.ConstantFailure)) {
      if (id == RecoveryNode) {
        const auto error = constant(Numeric::Value::FromBits(
            Type::U32, {static_cast<unsigned>(node.ConstantFailure), 0}));
        jump(Recovery, {error});
        return IR::None;
      }
      throw std::logic_error("unhandled constant failure reached Lower");
    }
    switch (node.Form) {
    case SemIR::Kind::Literal:
      throw std::logic_error("literal without a checked value");
    case SemIR::Kind::Group:
      return expression(node.Left);
    case SemIR::Kind::Local: {
      IR::Instruction instruction;
      instruction.Code = IR::Opcode::Load;
      instruction.Entity = node.Entity;
      return emit(std::move(instruction), node.ValueType);
    }
    case SemIR::Kind::Bind:
      return binding(node);
    case SemIR::Kind::Assign: {
      const auto value = expression(node.Right);
      if (Current != IR::None) {
        store(node.Entity, value);
      }
      return Current == IR::None ? IR::None : constant(Numeric::Value::Unit());
    }
    case SemIR::Kind::Discard:
      expression(node.Left);
      return Current == IR::None ? IR::None : constant(Numeric::Value::Unit());
    case SemIR::Kind::Return: {
      const auto value = node.Left == SemIR::None
                             ? constant(Numeric::Value::Unit())
                             : expression(node.Left);
      if (Current != IR::None) {
        IR::Terminator end;
        end.Kind = IR::Exit::Return;
        end.Operands = {value};
        terminate(std::move(end));
      }
      return IR::None;
    }
    case SemIR::Kind::Block:
      for (const auto statement : node.Statements) {
        expression(statement);
      }
      if (Current == IR::None) {
        return IR::None;
      }
      return node.Left == SemIR::None ? constant(Numeric::Value::Unit())
                                      : expression(node.Left);
    case SemIR::Kind::If:
      return branch(node);
    case SemIR::Kind::Call:
      return call(node);
    case SemIR::Kind::Binary:
      if (node.Operation == O::LogicalAnd || node.Operation == O::LogicalOr) {
        return logical(node);
      }
      [[fallthrough]];
    case SemIR::Kind::Unary:
    case SemIR::Kind::Convert: {
      std::vector<Id> operands;
      operands.push_back(expression(node.Left));
      if (node.Form == SemIR::Kind::Binary && Current != IR::None) {
        operands.push_back(expression(node.Right));
      }
      if (Current == IR::None) {
        return IR::None;
      }
      const auto savedRecovery = Recovery;
      if (id != RecoveryNode) {
        Recovery = IR::None;
      }
      const bool guarded =
          node.Form == SemIR::Kind::Convert ||
          (Target::IsInteger(node.ValueType) &&
           (node.Operation == O::Add || node.Operation == O::Subtract ||
            node.Operation == O::Multiply || node.Operation == O::Divide ||
            node.Operation == O::Remainder || node.Operation == O::Negate ||
            node.Operation == O::ShiftLeft || node.Operation == O::ShiftRight));
      Id value;
      if (guarded) {
        value = checked(node, std::move(operands));
      } else {
        IR::Instruction instruction;
        instruction.Code = node.Form == SemIR::Kind::Unary ? IR::Opcode::Unary
                                                           : IR::Opcode::Binary;
        instruction.Operation = node.Operation;
        instruction.Operands = std::move(operands);
        value = emit(std::move(instruction), node.ValueType);
      }
      Recovery = savedRecovery;
      return value;
    }
    }
    throw std::logic_error("unknown semantic node");
  }
  const SemIR::Storage &Source;
  IR::Draft Result;
  IR::Function *Function = nullptr;
  Id Current = IR::None;
  Id Recovery = IR::None;
  Id RecoveryNode = IR::None;
};
} // namespace
auto Lower(const SemIR::Program &program,
           Diagnostic::Diagnostics &diagnostics) -> std::optional<IR::Program> {
  try {
    return IR::Verify(Builder(program.storage()).run(), diagnostics);
  } catch (const std::logic_error &error) {
    diagnostics.error("lower", error.what());
    return std::nullopt;
  }
}
} // namespace Chtholly::Lower
