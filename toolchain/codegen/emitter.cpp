// SPDX-License-Identifier: MIT OR Apache-2.0
#include "emitter.h"
#include <Chtholly/CodeGen/codegen.h>
#include <Chtholly/Runtime/abi.h>

#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>

#include <array>
#include <stdexcept>

namespace Chtholly::CodeGen {
namespace {
using Type = Target::Type;
using Id = IR::Id;
using O = Numeric::Operation;
static_assert([] {
  const std::pair<Numeric::Failure, Runtime::FailureCode> categories[] = {
      {Numeric::Failure::None, Runtime::FailureCode::None},
      {Numeric::Failure::Overflow, Runtime::FailureCode::Overflow},
      {Numeric::Failure::DivisionByZero, Runtime::FailureCode::DivisionByZero},
      {Numeric::Failure::ShiftCount, Runtime::FailureCode::ShiftCount},
      {Numeric::Failure::OutOfRange, Runtime::FailureCode::OutOfRange},
      {Numeric::Failure::PrecisionLoss, Runtime::FailureCode::PrecisionLoss},
      {Numeric::Failure::NonFinite, Runtime::FailureCode::NonFinite},
      {Numeric::Failure::Fractional, Runtime::FailureCode::Fractional}};
  for (const auto &[host, target] : categories) {
    if (static_cast<unsigned>(host) != static_cast<unsigned>(target)) {
      return false;
    }
  }
  return true;
}());

auto OperationCode(O operation) -> unsigned {
  using Wire = Runtime::CheckedOperation;
  switch (operation) {
  case O::Negate:
    return static_cast<unsigned>(Wire::Negate);
  case O::Add:
    return static_cast<unsigned>(Wire::Add);
  case O::Subtract:
    return static_cast<unsigned>(Wire::Subtract);
  case O::Multiply:
    return static_cast<unsigned>(Wire::Multiply);
  case O::Divide:
    return static_cast<unsigned>(Wire::Divide);
  case O::Remainder:
    return static_cast<unsigned>(Wire::Remainder);
  case O::ShiftLeft:
    return static_cast<unsigned>(Wire::ShiftLeft);
  case O::ShiftRight:
    return static_cast<unsigned>(Wire::ShiftRight);
  default:
    throw std::logic_error("unexpected checked operation");
  }
}

// A value's defining block dominates every reachable non-phi use, but its
// array index need not be smaller. Reverse postorder visits dominators first.
// Block parameters are allocated separately before any body is emitted.
auto EmissionOrder(const IR::Function &function) -> std::vector<Id> {
  struct Visit {
    Id Block;
    unsigned NextSuccessor = 0;
  };
  std::vector<bool> seen(function.Blocks.size(), false);
  std::vector<Visit> pending{{0}};
  std::vector<Id> postorder;
  seen[0] = true;
  while (!pending.empty()) {
    auto &visit = pending.back();
    const auto &end = function.Blocks[visit.Block].End;
    unsigned successors = 0;
    if (end.Kind == IR::Exit::Jump) {
      successors = 1;
    } else if (end.Kind == IR::Exit::Branch || end.Kind == IR::Exit::Checked) {
      successors = 2;
    }
    if (visit.NextSuccessor == successors) {
      postorder.push_back(visit.Block);
      pending.pop_back();
      continue;
    }
    const auto next =
        visit.NextSuccessor++ == 0 ? end.First.Target : end.Second.Target;
    if (!seen[next]) {
      seen[next] = true;
      pending.push_back({next});
    }
  }
  return {postorder.rbegin(), postorder.rend()};
}
class Emitter {
public:
  Emitter(const IR::Draft &program, llvm::Module &module)
      : Program(program), Module(module), Context(module.getContext()),
        Builder(Context) {
    Builder.setIsFPConstrained(true);
    Builder.setDefaultConstrainedRounding(
        llvm::RoundingMode::NearestTiesToEven);
    Builder.setDefaultConstrainedExcept(llvm::fp::ebIgnore);
    Bits = llvm::StructType::get(Context,
                                 {Builder.getInt64Ty(), Builder.getInt64Ty()});
    const auto pointer = Builder.getPtrTy();
    Checked = Module.getOrInsertFunction(
        "chtholly_runtime_checked",
        llvm::FunctionType::get(
            Builder.getInt32Ty(),
            {Builder.getInt32Ty(), Builder.getInt32Ty(), Builder.getInt32Ty(),
             Builder.getInt32Ty(), Builder.getInt64Ty(), Builder.getInt64Ty(),
             Builder.getInt64Ty(), Builder.getInt64Ty(), pointer},
            false));
    Failure = Module.getOrInsertFunction(
        "chtholly_runtime_fail",
        llvm::FunctionType::get(Builder.getVoidTy(),
                                {Builder.getInt32Ty(), pointer,
                                 Builder.getInt64Ty(), Builder.getInt64Ty()},
                                false));
    Abort = Module.getOrInsertFunction(
        "chtholly_runtime_abort",
        llvm::FunctionType::get(Builder.getVoidTy(), false));
    llvm::cast<llvm::Function>(Failure.getCallee())
        ->addFnAttr(llvm::Attribute::NoReturn);
    llvm::cast<llvm::Function>(Abort.getCallee())
        ->addFnAttr(llvm::Attribute::NoReturn);
    FileName =
        Builder.CreateGlobalStringPtr(Program.SourceName, "source", 0, &Module);
    for (Id i = 0; i < Program.Functions.size(); ++i) {
      const auto &function = Program.Functions[i];
      if (!function.HasNormalEntry) {
        Functions.push_back(nullptr);
        continue;
      }
      std::vector<llvm::Type *> parameters;
      if (wide(function.ReturnType)) {
        parameters.push_back(pointer);
      }
      for (const auto &parameter : function.Parameters) {
        if (parameter.ValueType == Type::Unit) {
          continue;
        }
        if (wide(parameter.ValueType)) {
          parameters.push_back(Builder.getInt64Ty());
          parameters.push_back(Builder.getInt64Ty());
        } else {
          parameters.push_back(abiType(parameter.ValueType));
        }
      }
      const auto result = wide(function.ReturnType) ||
                                  function.ReturnType == Type::Unit ||
                                  function.ReturnType == Type::Never
                              ? Builder.getVoidTy()
                              : abiType(function.ReturnType);
      auto *generated = llvm::Function::Create(
          llvm::FunctionType::get(result, parameters, false),
          llvm::GlobalValue::ExternalLinkage, FunctionSymbol(i), Module);
      generated->setCallingConv(llvm::CallingConv::C);
      generated->addFnAttr(llvm::Attribute::StrictFP);
      if (function.ReturnType == Type::Never) {
        generated->addFnAttr(llvm::Attribute::NoReturn);
      }
      Functions.push_back(generated);
    }
  }
  auto run(bool nativeEntry) -> void {
    for (Id i = 0; i < Program.Functions.size(); ++i) {
      if (Functions[i]) {
        function(Program.Functions[i], *Functions[i]);
      }
    }
    if (nativeEntry) {
      auto *entry = llvm::Function::Create(
          llvm::FunctionType::get(Builder.getInt32Ty(), false),
          llvm::GlobalValue::ExternalLinkage, "main", Module);
      Builder.SetInsertPoint(llvm::BasicBlock::Create(Context, "entry", entry));
      Builder.CreateCall(Module.getOrInsertFunction(
          "chtholly_runtime_abi_v2",
          llvm::FunctionType::get(Builder.getVoidTy(), false)));
      Builder.CreateCall(Module.getOrInsertFunction(
          "chtholly_runtime_initialize",
          llvm::FunctionType::get(Builder.getVoidTy(), false)));
      Builder.CreateRet(Builder.CreateCall(Functions.at(Program.Entry)));
    }
  }

private:
  auto wide(Type type) const -> bool {
    return Program.Target.bits(type) == 128;
  }
  auto valueType(Type type) -> llvm::Type * {
    if (type == Type::F32) {
      return Builder.getFloatTy();
    }
    if (type == Type::F64) {
      return Builder.getDoubleTy();
    }
    if (type == Type::Unit || type == Type::Never) {
      return Builder.getVoidTy();
    }
    return llvm::IntegerType::get(Context, Program.Target.bits(type));
  }
  auto abiType(Type type) -> llvm::Type * {
    if (type == Type::Bool) {
      return Builder.getInt8Ty();
    }
    if (Target::IsInteger(type) && Program.Target.bits(type) < 32) {
      return Builder.getInt32Ty();
    }
    return valueType(type);
  }
  auto fromABI(llvm::Value *value, Type type) -> llvm::Value * {
    return value->getType() == valueType(type)
               ? value
               : Builder.CreateTrunc(value, valueType(type));
  }
  auto toABI(llvm::Value *value, Type type) -> llvm::Value * {
    if (value->getType() == abiType(type)) {
      return value;
    }
    return Target::IsSigned(type) ? Builder.CreateSExt(value, abiType(type))
                                  : Builder.CreateZExt(value, abiType(type));
  }
  auto words(llvm::Value *value,
             Type type) -> std::pair<llvm::Value *, llvm::Value *> {
    if (type == Type::Unit) {
      return {Builder.getInt64(0), Builder.getInt64(0)};
    }
    if (Target::IsFloat(type)) {
      value = Builder.CreateBitCast(
          value, llvm::IntegerType::get(Context, Program.Target.bits(type)));
    }
    if (wide(type)) {
      return {Builder.CreateTrunc(value, Builder.getInt64Ty()),
              Builder.CreateTrunc(Builder.CreateLShr(value, 64),
                                  Builder.getInt64Ty())};
    }
    return {Builder.CreateZExtOrTrunc(value, Builder.getInt64Ty()),
            Builder.getInt64(0)};
  }
  auto fromWords(llvm::Value *low, llvm::Value *high,
                 Type type) -> llvm::Value * {
    if (type == Type::Unit) {
      return nullptr;
    }
    if (wide(type)) {
      return Builder.CreateOr(
          Builder.CreateZExt(low, Builder.getInt128Ty()),
          Builder.CreateShl(Builder.CreateZExt(high, Builder.getInt128Ty()),
                            64));
    }
    auto *bits = Builder.CreateZExtOrTrunc(
        low, llvm::IntegerType::get(Context, Program.Target.bits(type)));
    return Target::IsFloat(type) ? Builder.CreateBitCast(bits, valueType(type))
                                 : bits;
  }
  auto readBits(llvm::Value *pointer, Type type) -> llvm::Value * {
    auto *low = Builder.CreateLoad(Builder.getInt64Ty(),
                                   Builder.CreateStructGEP(Bits, pointer, 0));
    auto *high = Builder.CreateLoad(Builder.getInt64Ty(),
                                    Builder.CreateStructGEP(Bits, pointer, 1));
    return fromWords(low, high, type);
  }
  auto writeBits(llvm::Value *pointer, llvm::Value *value, Type type) -> void {
    const auto [low, high] = words(value, type);
    Builder.CreateStore(low, Builder.CreateStructGEP(Bits, pointer, 0));
    Builder.CreateStore(high, Builder.CreateStructGEP(Bits, pointer, 1));
  }
  auto addEdge(const IR::Edge &edge, llvm::BasicBlock *from,
               llvm::Value *implicit = nullptr) -> void {
    const auto &parameters = Current->Blocks[edge.Target].Parameters;
    for (std::size_t i = 0; i < parameters.size(); ++i) {
      if (Current->Values[parameters[i]] == Type::Unit) {
        continue;
      }
      auto *phi = llvm::cast<llvm::PHINode>(Values[parameters[i]]);
      phi->addIncoming(implicit ? implicit : Values[edge.Arguments.at(i)],
                       from);
    }
  }
  auto unchecked(O op, llvm::Value *left, llvm::Value *right,
                 Type type) -> llvm::Value * {
    const bool floating = Target::IsFloat(type);
    const bool signedValue = Target::IsSigned(type);
    if (op >= O::Less && op <= O::NotEqual) {
      // Entries follow Less, LessEqual, Greater, GreaterEqual, Equal,
      // NotEqual. NaN is unordered: only != is true for that category.
      using Predicate = llvm::CmpInst::Predicate;
      constexpr std::array<Predicate, 6> FloatPredicates = {
          llvm::CmpInst::FCMP_OLT, llvm::CmpInst::FCMP_OLE,
          llvm::CmpInst::FCMP_OGT, llvm::CmpInst::FCMP_OGE,
          llvm::CmpInst::FCMP_OEQ, llvm::CmpInst::FCMP_UNE};
      constexpr std::array<Predicate, 6> SignedPredicates = {
          llvm::CmpInst::ICMP_SLT, llvm::CmpInst::ICMP_SLE,
          llvm::CmpInst::ICMP_SGT, llvm::CmpInst::ICMP_SGE,
          llvm::CmpInst::ICMP_EQ,  llvm::CmpInst::ICMP_NE};
      constexpr std::array<Predicate, 6> UnsignedPredicates = {
          llvm::CmpInst::ICMP_ULT, llvm::CmpInst::ICMP_ULE,
          llvm::CmpInst::ICMP_UGT, llvm::CmpInst::ICMP_UGE,
          llvm::CmpInst::ICMP_EQ,  llvm::CmpInst::ICMP_NE};
      const auto index =
          static_cast<unsigned>(op) - static_cast<unsigned>(O::Less);
      if (floating) {
        return Builder.CreateFCmp(FloatPredicates[index], left, right);
      }
      const auto predicate =
          (signedValue ? SignedPredicates : UnsignedPredicates)[index];
      return Builder.CreateICmp(predicate, left, right);
    }
    switch (op) {
    case O::Plus:
      return left;
    case O::Negate:
      return Builder.CreateFNeg(left);
    case O::Not:
    case O::Complement:
      return Builder.CreateNot(left);
    case O::BitAnd:
      return Builder.CreateAnd(left, right);
    case O::BitOr:
      return Builder.CreateOr(left, right);
    case O::BitXor:
      return Builder.CreateXor(left, right);
    case O::Add:
      return Builder.CreateFAdd(left, right);
    case O::Subtract:
      return Builder.CreateFSub(left, right);
    case O::Multiply:
      return Builder.CreateFMul(left, right);
    case O::Divide:
      return Builder.CreateFDiv(left, right);
    default:
      throw std::logic_error("unverified numeric instruction");
    }
  }
  auto function(const IR::Function &input, llvm::Function &output) -> void {
    Current = &input;
    Values.assign(input.Values.size(), nullptr);
    Blocks.clear();
    for (std::size_t i = 0; i < input.Blocks.size(); ++i) {
      Blocks.push_back(
          llvm::BasicBlock::Create(Context, "b" + std::to_string(i), &output));
    }
    Builder.SetInsertPoint(Blocks[0]);
    auto argument = output.arg_begin();
    llvm::Value *returnAddress =
        wide(input.ReturnType) ? &*argument++ : nullptr;
    for (std::size_t p = 0; p < input.Parameters.size(); ++p) {
      const auto type = input.Parameters[p].ValueType;
      const auto value = input.Blocks[0].Parameters[p];
      if (type == Type::Unit) {
        continue;
      }
      if (wide(type)) {
        auto *low = &*argument++;
        auto *high = &*argument++;
        Values[value] = fromWords(low, high, type);
      } else {
        Values[value] = fromABI(&*argument++, type);
      }
    }
    std::vector<llvm::Value *> locals;
    for (const auto type : input.Locals) {
      locals.push_back(type == Type::Unit || type == Type::Never
                           ? nullptr
                           : Builder.CreateAlloca(valueType(type)));
    }
    auto *scratch = Builder.CreateAlloca(Bits);
    for (std::size_t b = 1; b < input.Blocks.size(); ++b) {
      Builder.SetInsertPoint(Blocks[b]);
      for (const auto value : input.Blocks[b].Parameters) {
        if (input.Values[value] != Type::Unit) {
          Values[value] = Builder.CreatePHI(valueType(input.Values[value]), 2);
        }
      }
    }
    const auto order = EmissionOrder(input);
    std::vector<bool> emitted(input.Blocks.size(), false);
    for (const auto b : order) {
      emitted[b] = true;
      Builder.SetInsertPoint(Blocks[b]);
      for (const auto &instruction : input.Blocks[b].Instructions) {
        llvm::Value *result = nullptr;
        switch (instruction.Code) {
        case IR::Opcode::Constant: {
          const auto type = instruction.Constant->type();
          if (type != Type::Unit) {
            const auto words = instruction.Constant->bits();
            const llvm::APInt bits(Program.Target.bits(type),
                                   llvm::ArrayRef<std::uint64_t>(
                                       words.data(), wide(type) ? 2 : 1));
            if (Target::IsFloat(type)) {
              result = llvm::ConstantFP::get(
                  Context,
                  llvm::APFloat(type == Type::F32 ? llvm::APFloat::IEEEsingle()
                                                  : llvm::APFloat::IEEEdouble(),
                                bits));
            } else {
              result = llvm::ConstantInt::get(Context, bits);
            }
          }
          break;
        }
        case IR::Opcode::Load:
          if (locals[instruction.Entity]) {
            result =
                Builder.CreateLoad(valueType(input.Locals[instruction.Entity]),
                                   locals[instruction.Entity]);
          }
          break;
        case IR::Opcode::Store:
          if (locals[instruction.Entity]) {
            Builder.CreateStore(Values[instruction.Operands[0]],
                                locals[instruction.Entity]);
          }
          break;
        case IR::Opcode::Unary:
        case IR::Opcode::Binary:
          result =
              unchecked(instruction.Operation, Values[instruction.Operands[0]],
                        instruction.Operands.size() == 1
                            ? nullptr
                            : Values[instruction.Operands[1]],
                        input.Values[instruction.Operands[0]]);
          break;
        case IR::Opcode::Call: {
          const auto &callee = Program.Functions[instruction.Entity];
          std::vector<llvm::Value *> arguments;
          if (wide(callee.ReturnType)) {
            arguments.push_back(scratch);
          }
          for (std::size_t p = 0; p < instruction.Operands.size(); ++p) {
            const auto type = callee.Parameters[p].ValueType;
            auto *value = Values[instruction.Operands[p]];
            if (type == Type::Unit) {
              continue;
            }
            if (wide(type)) {
              const auto [low, high] = words(value, type);
              arguments.push_back(low);
              arguments.push_back(high);
            } else {
              arguments.push_back(toABI(value, type));
            }
          }
          auto *call =
              Builder.CreateCall(Functions[instruction.Entity], arguments);
          if (wide(callee.ReturnType)) {
            result = readBits(scratch, callee.ReturnType);
          } else if (callee.ReturnType != Type::Unit &&
                     callee.ReturnType != Type::Never) {
            result = fromABI(call, callee.ReturnType);
          }
          break;
        }
        }
        if (instruction.Result != IR::None) {
          Values[instruction.Result] = result;
        }
      }
      const auto &end = input.Blocks[b].End;
      switch (end.Kind) {
      case IR::Exit::Jump:
        addEdge(end.First, Builder.GetInsertBlock());
        Builder.CreateBr(Blocks[end.First.Target]);
        break;
      case IR::Exit::Branch:
        if (end.First.Target == end.Second.Target) {
          // LLVM PHIs distinguish predecessor blocks, not parallel edges.
          // Give each edge its own predecessor so its arguments stay distinct.
          auto *first =
              llvm::BasicBlock::Create(Context, "branch.true", &output);
          auto *second =
              llvm::BasicBlock::Create(Context, "branch.false", &output);
          Builder.CreateCondBr(Values[end.Operands[0]], first, second);
          Builder.SetInsertPoint(first);
          addEdge(end.First, first);
          Builder.CreateBr(Blocks[end.First.Target]);
          Builder.SetInsertPoint(second);
          addEdge(end.Second, second);
          Builder.CreateBr(Blocks[end.Second.Target]);
          break;
        }
        addEdge(end.First, Builder.GetInsertBlock());
        addEdge(end.Second, Builder.GetInsertBlock());
        Builder.CreateCondBr(Values[end.Operands[0]], Blocks[end.First.Target],
                             Blocks[end.Second.Target]);
        break;
      case IR::Exit::Checked: {
        const auto leftType = input.Values[end.Operands[0]];
        const auto rightType =
            end.Operands.size() == 2 ? input.Values[end.Operands[1]] : leftType;
        const auto [leftLow, leftHigh] =
            words(Values[end.Operands[0]], leftType);
        const auto [rightLow, rightHigh] =
            end.Operands.size() == 2
                ? words(Values[end.Operands[1]], rightType)
                : std::pair<llvm::Value *, llvm::Value *>{Builder.getInt64(0),
                                                          Builder.getInt64(0)};
        auto *status = Builder.CreateCall(
            Checked,
            {Builder.getInt32(
                 end.Conversion
                     ? static_cast<unsigned>(Runtime::CheckedOperation::Convert)
                     : OperationCode(end.Operation)),
             Builder.getInt32(static_cast<unsigned>(leftType)),
             Builder.getInt32(static_cast<unsigned>(end.ResultType)),
             Builder.getInt32(static_cast<unsigned>(rightType)), leftLow,
             leftHigh, rightLow, rightHigh, scratch});
        auto *success =
            llvm::BasicBlock::Create(Context, "checked.success", &output);
        addEdge(end.Second, Builder.GetInsertBlock(), status);
        Builder.CreateCondBr(Builder.CreateICmpEQ(status, Builder.getInt32(0)),
                             success, Blocks[end.Second.Target]);
        Builder.SetInsertPoint(success);
        auto *value = readBits(scratch, end.ResultType);
        addEdge(end.First, success, value);
        Builder.CreateBr(Blocks[end.First.Target]);
        break;
      }
      case IR::Exit::Return:
        if (wide(input.ReturnType)) {
          writeBits(returnAddress, Values[end.Operands[0]], input.ReturnType);
          Builder.CreateRetVoid();
        } else if (input.ReturnType == Type::Unit) {
          Builder.CreateRetVoid();
        } else {
          Builder.CreateRet(toABI(Values[end.Operands[0]], input.ReturnType));
        }
        break;
      case IR::Exit::Fail:
        Builder.CreateCall(Failure, {Values[end.Operands[0]], FileName,
                                     Builder.getInt64(end.Location.Line),
                                     Builder.getInt64(end.Location.Column)});
        Builder.CreateUnreachable();
        break;
      case IR::Exit::Abort:
        Builder.CreateCall(Abort);
        Builder.CreateUnreachable();
        break;
      case IR::Exit::Unreachable:
        Builder.CreateUnreachable();
        break;
      case IR::Exit::Missing:
        throw std::logic_error("missing verified terminator");
      }
    }
    // No reachable use can depend on these blocks (the verifier proves that).
    // Do not emit their instructions or edges into reachable phi nodes.
    for (std::size_t b = 0; b < input.Blocks.size(); ++b) {
      if (!emitted[b]) {
        Blocks[b]->dropAllReferences();
        Blocks[b]->eraseFromParent();
      }
    }
  }
  const IR::Draft &Program;
  llvm::Module &Module;
  llvm::LLVMContext &Context;
  llvm::IRBuilder<> Builder;
  llvm::StructType *Bits = nullptr;
  llvm::Value *FileName = nullptr;
  llvm::FunctionCallee Checked, Failure, Abort;
  std::vector<llvm::Function *> Functions;
  const IR::Function *Current = nullptr;
  std::vector<llvm::Value *> Values;
  std::vector<llvm::BasicBlock *> Blocks;
};
} // namespace
auto EmitModule(const IR::Draft &program, llvm::Module &module,
                bool nativeEntry) -> void {
  Emitter(program, module).run(nativeEntry);
}
} // namespace Chtholly::CodeGen
