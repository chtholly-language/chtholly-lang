// SPDX-License-Identifier: MIT OR Apache-2.0
#include "checker.h"

namespace Chtholly::Check {
using Type = Target::Type;
using Id = SemIR::Id;
using O = Numeric::Operation;

auto Checker::finish(Id node, Numeric::Result result,
                     bool reportEvaluation) -> void {
  auto &checked = Result.Nodes[node];
  checked.Constant = std::move(result.ValueResult);
  checked.ConstantFailure = result.Error;
  const bool compilerFailure =
      result.Error == Numeric::Failure::ResourceExceeded ||
      result.Error == Numeric::Failure::InternalError;
  const bool unhandledEvaluation = result.Error != Numeric::Failure::None &&
                                   reportEvaluation && RecoveryRoot != node;
  if (compilerFailure || unhandledEvaluation) {
    fail(node, std::string(Numeric::FailureName(result.Error)));
  }
}
auto Checker::literal(Id node, Type context, bool negative) -> Type {
  const auto &syntax = Tree.Nodes[node];
  auto &checked = Result.Nodes[node];
  checked.Form = SemIR::Kind::Literal;
  auto text = Source.slice(syntax.Text);
  Type valueType;
  if (syntax.SuffixLength != 0) {
    valueType = Target::Lookup(text.substr(text.size() - syntax.SuffixLength))
                    .value_or(Type::Invalid);
    text.remove_suffix(syntax.SuffixLength);
  } else if (syntax.Form == Parse::Kind::Float) {
    valueType = Target::IsFloat(context) ? context : Type::F64;
  } else {
    valueType =
        Target::IsInteger(context) || context == Type::MathematicalInteger
            ? context
            : Type::I32;
  }
  checked.ValueType = valueType;
  if (syntax.Radix != 10) {
    text.remove_prefix(2);
  }
  // Literal formation remains mandatory even on a path that cannot run.
  auto formed =
      Numeric::Literal(text, syntax.Radix, valueType, negative, Budget);
  checked.Constant = std::move(formed.ValueResult);
  checked.ConstantFailure = formed.Error;
  if (formed.Error != Numeric::Failure::None) {
    fail(node, "cannot form a literal of type " +
                   std::string(Target::Name(valueType)) + ": " +
                   std::string(Numeric::FailureName(formed.Error)));
  }
  expect(node, context);
  return valueType;
}
auto Checker::unary(Id node, Type context, bool executable) -> Type {
  const auto &syntax = Tree.Nodes[node];
  auto &checked = Result.Nodes[node];
  checked.Form = SemIR::Kind::Unary;
  checked.Operation = operation(syntax.Operator, true);
  checked.Left = syntax.Left;
  const auto operand = root(syntax.Left);
  const auto candidate = hint(operand);
  const auto signedContext = candidate == Type::Invalid ? context : candidate;
  const bool minimumLiteral =
      checked.Operation == O::Negate &&
      Tree.Nodes[operand].Form == Parse::Kind::Integer &&
      (Target::IsSigned(signedContext) || signedContext == Type::Invalid ||
       signedContext == Type::MathematicalInteger);
  if (minimumLiteral) {
    Result.Nodes[operand].Executable = executable;
    literal(operand, context, true);
    // Group nodes must still receive checked identities. The unary node owns
    // the combined literal; Lower never reads a positive out-of-range operand.
    checked.ValueType = Result.Nodes[operand].ValueType;
    checked.Constant = Result.Nodes[operand].Constant;
    for (auto group = syntax.Left; group != operand;
         group = Tree.Nodes[group].Left) {
      auto &record = Result.Nodes[group];
      record.Form = SemIR::Kind::Group;
      record.Executable = executable;
      record.ValueType = checked.ValueType;
      record.Left = Tree.Nodes[group].Left;
      record.Constant = checked.Constant;
    }
    return checked.ValueType;
  }
  auto operandContext = context;
  if (checked.Operation == O::Not) {
    operandContext = Type::Bool;
  } else if (!(Target::IsNumeric(context) ||
               context == Type::MathematicalInteger)) {
    operandContext = Type::Invalid;
  }
  const auto operandType = expression(syntax.Left, operandContext, executable);
  bool eligible = false;
  if (operandType == Type::Never || operandType == Type::Invalid) {
    eligible = true;
  } else if (checked.Operation == O::Not) {
    eligible = operandType == Type::Bool;
  } else if (checked.Operation == O::Complement) {
    eligible = Target::IsInteger(operandType);
  } else {
    eligible = Target::IsNumeric(operandType) ||
               operandType == Type::MathematicalInteger;
  }
  if (!eligible) {
    fail(node, "operand does not support this unary operation");
  }
  checked.ValueType = operandType;
  checked.Completes = Result.Nodes[syntax.Left].Completes;
  if (eligible && Result.Nodes[syntax.Left].Constant) {
    finish(node,
           Numeric::Unary(checked.Operation,
                          *Result.Nodes[syntax.Left].Constant, Budget),
           executable);
  }
  return checked.ValueType;
}
auto Checker::binary(Id node, Type context, bool executable) -> Type {
  const auto &syntax = Tree.Nodes[node];
  auto &checked = Result.Nodes[node];
  checked.Form = SemIR::Kind::Binary;
  checked.Operation = operation(syntax.Operator);
  checked.Left = syntax.Left;
  checked.Right = syntax.Right;
  const bool logical =
      checked.Operation == O::LogicalAnd || checked.Operation == O::LogicalOr;
  const bool comparison =
      checked.Operation >= O::Less && checked.Operation <= O::NotEqual;
  const bool shift =
      checked.Operation == O::ShiftLeft || checked.Operation == O::ShiftRight;
  auto operandContext = logical      ? Type::Bool
                        : comparison ? Type::Invalid
                                     : context;
  if (!logical && !comparison &&
      !(Target::IsNumeric(operandContext) ||
        operandContext == Type::MathematicalInteger)) {
    operandContext = Type::Invalid;
  }
  if (!logical) {
    auto knownType = hint(syntax.Left);
    if (knownType == Type::Never) {
      knownType = Type::Invalid;
    }
    if (knownType == Type::Invalid && !shift) {
      knownType = hint(syntax.Right);
    }
    if (knownType != Type::Invalid && knownType != Type::Never) {
      operandContext = knownType;
    }
  }
  auto leftType = expression(syntax.Left, operandContext, executable);
  const auto &left = Result.Nodes[syntax.Left];
  bool rightExecutable = executable && left.Completes;
  if (logical && left.Constant && left.ValueType == Type::Bool) {
    const auto condition = left.Constant->boolean();
    rightExecutable &=
        checked.Operation == O::LogicalAnd ? condition : !condition;
  }
  const auto rightContext = shift                     ? Type::Invalid
                            : leftType == Type::Never ? operandContext
                                                      : leftType;
  auto rightType = expression(syntax.Right, rightContext, rightExecutable);
  const auto &right = Result.Nodes[syntax.Right];
  const auto base = leftType == Type::Never ? rightType : leftType;
  const auto compatible = leftType == rightType || leftType == Type::Never ||
                          rightType == Type::Never;
  bool eligible = compatible;
  if (logical) {
    eligible &= (leftType == Type::Bool || leftType == Type::Never) &&
                (rightType == Type::Bool || rightType == Type::Never);
  } else if (shift) {
    eligible = (Target::IsInteger(leftType) || leftType == Type::Never) &&
               (Target::IsInteger(rightType) || rightType == Type::Never);
  } else if (comparison) {
    const bool equality =
        checked.Operation == O::Equal || checked.Operation == O::NotEqual;
    eligible &= Target::IsNumeric(base) || base == Type::Never ||
                (equality && base == Type::Bool);
  } else if (checked.Operation == O::BitAnd || checked.Operation == O::BitOr ||
             checked.Operation == O::BitXor ||
             checked.Operation == O::Remainder) {
    eligible &= Target::IsInteger(base) || base == Type::MathematicalInteger ||
                base == Type::Never;
  } else {
    eligible &= Target::IsNumeric(base) || base == Type::MathematicalInteger ||
                base == Type::Never;
  }
  if (!eligible && leftType != Type::Invalid && rightType != Type::Invalid) {
    fail(node, "operands do not support this operation with these types");
  }
  checked.ValueType = logical || comparison ? Type::Bool : base;
  checked.Completes = left.Completes && (logical || right.Completes);
  if (logical && left.Constant && left.ValueType == Type::Bool) {
    const bool evaluatesRight = checked.Operation == O::LogicalAnd
                                    ? left.Constant->boolean()
                                    : !left.Constant->boolean();
    if (evaluatesRight) {
      checked.Completes = left.Completes && right.Completes;
    }
  }
  if (logical && left.Constant && left.ValueType == Type::Bool &&
      !rightExecutable && left.Completes) {
    checked.Constant = left.Constant;
    checked.Completes = true;
  } else if (eligible && left.Constant && right.Constant) {
    finish(node,
           Numeric::Binary(checked.Operation, *left.Constant, *right.Constant,
                           Budget),
           executable);
  }
  return checked.ValueType;
}
auto Checker::conversion(Id node, bool executable) -> Type {
  const auto &syntax = Tree.Nodes[node];
  auto &checked = Result.Nodes[node];
  checked.Form = SemIR::Kind::Convert;
  checked.Left = syntax.Left;
  checked.ValueType = type(syntax.Annotation);
  const auto sourceContext =
      mathematical(syntax.Left) ? Type::MathematicalInteger : Type::Invalid;
  const auto sourceType = expression(syntax.Left, sourceContext, executable);
  const auto &source = Result.Nodes[syntax.Left];
  checked.Completes = source.Completes;
  const bool eligible =
      Target::IsNumeric(checked.ValueType) &&
      (Target::IsNumeric(sourceType) ||
       sourceType == Type::MathematicalInteger || sourceType == Type::Never);
  if (!eligible) {
    fail(node, "as requires numeric source and target types");
  }
  if (eligible && source.Constant) {
    finish(node, Numeric::Convert(*source.Constant, checked.ValueType, Budget),
           executable);
  }
  return checked.ValueType;
}
auto Checker::expression(Id node, Type context, bool executable) -> Type {
  if (ExpressionDepth++ == 0) {
    Budget = Numeric::Budget(Budget.limits());
  }
  struct DepthGuard {
    unsigned &Depth;
    ~DepthGuard() { --Depth; }
  } guard{ExpressionDepth};
  auto &checked = Result.Nodes[node];
  const auto &syntax = Tree.Nodes[node];
  checked.Executable = executable;
  Type result = Type::Invalid;
  switch (syntax.Form) {
  case Parse::Kind::Integer:
  case Parse::Kind::Float:
    return literal(node, context, false);
  case Parse::Kind::Boolean:
    checked.ValueType = Type::Bool;
    checked.Constant = Numeric::Value::Boolean(syntax.Boolean);
    result = Type::Bool;
    break;
  case Parse::Kind::Unit:
    checked.ValueType = Type::Unit;
    checked.Constant = Numeric::Value::Unit();
    result = Type::Unit;
    break;
  case Parse::Kind::Name:
    checked.Form = SemIR::Kind::Local;
    if (const auto local = lookup(name(syntax.Text))) {
      checked.Entity = *local;
      checked.ValueType =
          Result.Functions[CurrentFunction].Locals[*local].ValueType;
      checked.Completes = checked.ValueType != Type::Never;
      result = checked.ValueType;
    } else {
      fail(node, "unknown local name or function used as a value");
    }
    break;
  case Parse::Kind::Group:
    checked.Form = SemIR::Kind::Group;
    checked.Left = syntax.Left;
    result = expression(syntax.Left, context, executable);
    checked.ValueType = result;
    checked.Completes = Result.Nodes[syntax.Left].Completes;
    checked.Constant = Result.Nodes[syntax.Left].Constant;
    checked.ConstantFailure = Result.Nodes[syntax.Left].ConstantFailure;
    break;
  case Parse::Kind::Unary:
    result = unary(node, context, executable);
    break;
  case Parse::Kind::Binary:
    result = binary(node, context, executable);
    break;
  case Parse::Kind::Convert:
    result = conversion(node, executable);
    break;
  case Parse::Kind::Call:
    result = call(node, executable);
    break;
  case Parse::Kind::Block:
    result = block(node, context, executable);
    break;
  case Parse::Kind::If:
    result = branch(node, context, executable);
    break;
  default:
    fail(node, "statement is not an expression");
    break;
  }
  expect(node, context);
  return result;
}
} // namespace Chtholly::Check
