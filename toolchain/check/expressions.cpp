// SPDX-License-Identifier: MIT OR Apache-2.0
#include "checker.h"

namespace Chtholly::Check {
using Type = Types::TypeId;
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
auto Checker::literal(Id node, Type context, bool negative,
                      SemIR::Node *destination) -> Type {
  const auto &syntax = Tree.Nodes[node];
  auto &checked = destination ? *destination : Result.Nodes[node];
  checked.Form = SemIR::Kind::Literal;
  auto text = Source.slice(syntax.Text);
  Type valueType;
  if (syntax.SuffixLength != 0) {
    valueType = Types::Lookup(text.substr(text.size() - syntax.SuffixLength))
                    .value_or(Type::Invalid);
    text.remove_suffix(syntax.SuffixLength);
  } else if (syntax.Form == Parse::Kind::Float) {
    valueType = Target::IsFloat(context) ? context : Type::F64;
  } else {
    valueType = Target::IsInteger(context) || MathContext ? context : Type::I32;
  }
  checked.ValueType = valueType;
  if (syntax.Radix != 10) {
    text.remove_prefix(2);
  }
  // Literal formation remains mandatory even on a path that cannot run.
  const bool mathematicalLiteral = MathContext && syntax.SuffixLength == 0 &&
                                   syntax.Form == Parse::Kind::Integer;
  auto formed =
      mathematicalLiteral
          ? Numeric::MathematicalLiteral(text, syntax.Radix, negative, Budget)
          : Numeric::Literal(text, syntax.Radix, Target::ScalarOf(valueType),
                             negative, Budget);
  checked.Constant = std::move(formed.ValueResult);
  checked.ConstantFailure = formed.Error;
  if (formed.Error != Numeric::Failure::None) {
    fail(node, "cannot form a literal of type " + describe(valueType) + ": " +
                   std::string(Numeric::FailureName(formed.Error)));
  }
  if (!destination) {
    expect(node, context);
  }
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
      (Target::IsSigned(signedContext) || signedContext == Type::Invalid);
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
  } else if (!(Target::IsNumeric(context) || context == Type::Invalid)) {
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
    eligible = Target::IsNumeric(operandType) || operandType == Type::Invalid;
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
      !(Target::IsNumeric(operandContext) || operandContext == Type::Invalid)) {
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
    eligible &=
        Target::IsInteger(base) || base == Type::Invalid || base == Type::Never;
  } else {
    eligible &=
        Target::IsNumeric(base) || base == Type::Invalid || base == Type::Never;
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
  const auto sourceContext = Type::Invalid;
  const auto oldMath = MathContext;
  MathContext = mathematical(syntax.Left);
  const auto sourceType = expression(syntax.Left, sourceContext, executable);
  MathContext = oldMath;
  const auto &source = Result.Nodes[syntax.Left];
  checked.Completes = source.Completes;
  const bool eligible =
      Target::IsNumeric(checked.ValueType) &&
      (Target::IsNumeric(sourceType) || sourceType == Type::Invalid ||
       sourceType == Type::Never);
  if (!eligible) {
    fail(node, "as requires numeric source and target types");
  }
  if (eligible && source.Constant) {
    finish(node, Numeric::Convert(*source.Constant, checked.ValueType, Budget),
           executable);
  }
  return checked.ValueType;
}
auto Checker::expression(Id node, Type context, bool executable,
                         bool locateOnly) -> Type {
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
      const auto &binding = Result.Functions[CurrentFunction].Locals[*local];
      checked.ValueType =
          Result.Functions[CurrentFunction].Locals[*local].ValueType;
      checked.ValueCategory =
          binding.Trial ? SemIR::Category::TrialPlace : SemIR::Category::Place;
      checked.Writable = binding.Writable && !binding.Trial;
      checked.Completes = checked.ValueType != Type::Invalid &&
                          TypeBuilder.inhabited(checked.ValueType);
      result = checked.ValueType;
    } else if (const auto found = Constants.find(name(syntax.Text));
               found != Constants.end()) {
      checked.Form = SemIR::Kind::Literal;
      checked.Constant = constant(found->second);
      checked.ValueType = checked.Constant
                              ? Target::SourceType(checked.Constant->type())
                              : Type::Invalid;
      result = checked.ValueType;
    } else {
      fail(node, "unknown local name or function used as a value");
    }
    break;
  case Parse::Kind::Group:
    checked.Form = SemIR::Kind::Group;
    checked.Left = syntax.Left;
    result = expression(syntax.Left, context, executable, locateOnly);
    checked.ValueType = result;
    checked.Completes = Result.Nodes[syntax.Left].Completes;
    checked.Constant = Result.Nodes[syntax.Left].Constant;
    checked.ConstantFailure = Result.Nodes[syntax.Left].ConstantFailure;
    checked.ValueCategory = Result.Nodes[syntax.Left].ValueCategory;
    checked.Writable = Result.Nodes[syntax.Left].Writable;
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
  case Parse::Kind::Tuple:
  case Parse::Kind::Array:
  case Parse::Kind::Repeat:
  case Parse::Kind::Construct:
    result = aggregate(node, context, executable);
    break;
  case Parse::Kind::Project:
  case Parse::Kind::Index:
    result = projection(node, executable);
    break;
  case Parse::Kind::Copy:
  case Parse::Kind::Move:
    checked.Form = syntax.Form == Parse::Kind::Copy ? SemIR::Kind::Copy
                                                    : SemIR::Kind::Move;
    checked.Left = syntax.Left;
    checked.ValueType = result = expression(syntax.Left, context, executable);
    checked.Completes = Result.Nodes[syntax.Left].Completes;
    if (syntax.Form == Parse::Kind::Copy && result != Type::Invalid &&
        !TypeBuilder.copy(result)) {
      fail(node, "explicit copy requires Copy qualification");
    }
    if (Result.Nodes[syntax.Left].ValueCategory ==
            SemIR::Category::TrialPlace &&
        syntax.Form == Parse::Kind::Move) {
      fail(node, "cannot move a trial pattern binding");
    }
    if (syntax.Form == Parse::Kind::Move && hasDynamicProjection(syntax.Left)) {
      fail(node, "cannot move out of a dynamic array element, including Copy "
                 "elements");
    }
    break;
  case Parse::Kind::Loop:
  case Parse::Kind::While:
    result = loop(node, context, executable);
    break;
  case Parse::Kind::Match:
    result = match(node, context, executable);
    break;
  default:
    fail(node, "statement is not an expression");
    break;
  }
  if (!locateOnly && checked.ValueCategory == SemIR::Category::TrialPlace &&
      result != Type::Invalid && !TypeBuilder.copy(result)) {
    fail(node, "trial pattern bindings can only expose Copy observations "
               "before guard commitment");
  }
  if (!locateOnly && result != Type::Invalid &&
      (checked.Form == SemIR::Kind::Index ||
       checked.Form == SemIR::Kind::Project) &&
      hasDynamicProjection(node) && !TypeBuilder.copy(result)) {
    fail(node, "taking a dynamic array element requires Copy; dynamic move is "
               "not supported");
  }
  expect(node, context);
  return result;
}
} // namespace Chtholly::Check
