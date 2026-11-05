// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Numeric/numeric.h>

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/APSInt.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/Error.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Chtholly::Numeric {
using Target::Type;

struct Value::Storage {
  explicit Storage(llvm::APInt integer) : Integer(std::move(integer)) {}
  explicit Storage(llvm::APFloat value) : Float(std::move(value)) {}

  llvm::APInt Integer{1, 0};
  llvm::APFloat Float{llvm::APFloat::IEEEdouble()};
};

struct Access {
  static auto Get(const Value &value) -> const Value::Storage & {
    if (!value.Data) {
      throw std::logic_error("numeric value has no storage");
    }
    return *value.Data;
  }
  static auto Integer(Type type, llvm::APInt value) -> Value {
    auto storage = std::make_shared<const Value::Storage>(std::move(value));
    return Value(type, std::move(storage));
  }
  static auto Float(Type type, llvm::APFloat value) -> Value {
    auto storage = std::make_shared<const Value::Storage>(std::move(value));
    return Value(type, std::move(storage));
  }
};

namespace {
auto Success(Value value) -> Result {
  return {std::move(value), Failure::None};
}
auto Error(Failure failure) -> Result { return {std::nullopt, failure}; }
auto Semantics(Type type) -> const llvm::fltSemantics & {
  return type == Type::F32 ? llvm::APFloat::IEEEsingle()
                           : llvm::APFloat::IEEEdouble();
}
auto Mathematics(const Value &value) -> llvm::APInt {
  const auto &bits = Access::Get(value).Integer;
  if (value.type() == Type::MathematicalInteger) {
    return bits;
  }
  return Target::IsSigned(value.type()) ? bits.sext(bits.getBitWidth() + 1)
                                        : bits.zext(bits.getBitWidth() + 1);
}
auto InRange(const llvm::APInt &integer, Type type,
             Target::Description target) -> bool {
  const auto bits = target.bits(type);
  if (!Target::IsInteger(type)) {
    return false;
  }
  return Target::IsSigned(type) ? integer.isSignedIntN(bits)
                                : !integer.isNegative() && integer.isIntN(bits);
}
auto FinishInteger(llvm::APInt value, Type type, Budget &budget,
                   Target::Description target, Failure failure) -> Result {
  if (value.getBitWidth() > budget.limits().IntegerBits) {
    return Error(Failure::ResourceExceeded);
  }
  if (type == Type::MathematicalInteger) {
    return Success(Access::Integer(type, std::move(value)));
  }
  if (!InRange(value, type, target)) {
    return Error(failure);
  }
  return Success(Access::Integer(type, value.sextOrTrunc(target.bits(type))));
}
auto IntegerFloatBound(Type type) -> llvm::APInt {
  // Largest finite binary32 = (2^24-1)*2^104; binary64 analogously.
  const unsigned width = type == Type::F32 ? 129 : 1025;
  const unsigned precision = type == Type::F32 ? 24 : 53;
  const unsigned shift = type == Type::F32 ? 104 : 971;
  return llvm::APInt::getAllOnes(precision).zext(width).shl(shift);
}
auto IntegerToFloat(const Value &value, Type type, Budget &budget) -> Result {
  auto integer = Mathematics(value);
  auto bound = IntegerFloatBound(type);
  const auto width = std::max(integer.getBitWidth() + 1, bound.getBitWidth());
  if (width > budget.limits().IntegerBits) {
    return Error(Failure::ResourceExceeded);
  }
  integer = integer.sext(width);
  const auto magnitude = integer.isNegative() ? -integer : integer;
  if (magnitude.ugt(bound.zext(width))) {
    return Error(Failure::OutOfRange);
  }
  llvm::APFloat result(Semantics(type));
  const auto status = result.convertFromAPInt(
      integer, true, llvm::APFloat::rmNearestTiesToEven);
  if ((status & llvm::APFloat::opInexact) != 0) {
    return Error(Failure::PrecisionLoss);
  }
  return Success(Access::Float(type, std::move(result)));
}
auto FloatToInteger(const Value &value, Type type, Budget &budget,
                    Target::Description target) -> Result {
  const auto &source = Access::Get(value).Float;
  if (!source.isFinite()) {
    return Error(Failure::NonFinite);
  }
  // APFloat reports loss of the sign of -0 through its exactness flag.
  // Both IEEE zero signs have the same integer value under Chtholly's as.
  if (source.isZero()) {
    return FinishInteger(llvm::APInt(2, 0), type, budget, target,
                         Failure::OutOfRange);
  }
  auto integral = source;
  integral.roundToIntegral(llvm::APFloat::rmTowardZero);
  if (integral.compare(source) != llvm::APFloat::cmpEqual) {
    return Error(Failure::Fractional);
  }
  llvm::APSInt integer(target.bits(type) + 2, false);
  bool exact = false;
  const auto status =
      source.convertToInteger(integer, llvm::APFloat::rmTowardZero, &exact);
  if ((status & llvm::APFloat::opInvalidOp) != 0 || !exact) {
    return Error(Failure::OutOfRange);
  }
  return FinishInteger(integer, type, budget, target, Failure::OutOfRange);
}
auto Comparison(Operation op, int comparison) -> bool {
  switch (op) {
  case Operation::Equal:
    return comparison == 0;
  case Operation::NotEqual:
    return comparison != 0;
  case Operation::Less:
    return comparison < 0;
  case Operation::LessEqual:
    return comparison <= 0;
  case Operation::Greater:
    return comparison > 0;
  case Operation::GreaterEqual:
    return comparison >= 0;
  default:
    return false;
  }
}
auto IsComparison(Operation op) -> bool {
  return op >= Operation::Less && op <= Operation::NotEqual;
}
} // namespace

auto FailureName(Failure failure) -> std::string_view {
  switch (failure) {
  case Failure::None:
    return "None";
  case Failure::Overflow:
    return "Arithmetic.Overflow";
  case Failure::DivisionByZero:
    return "Arithmetic.DivisionByZero";
  case Failure::ShiftCount:
    return "Arithmetic.ShiftCount";
  case Failure::OutOfRange:
    return "Conversion.OutOfRange";
  case Failure::PrecisionLoss:
    return "Conversion.PrecisionLoss";
  case Failure::NonFinite:
    return "Conversion.NonFinite";
  case Failure::Fractional:
    return "Conversion.Fractional";
  case Failure::ResourceExceeded:
    return "ResourceExceeded";
  case Failure::InternalError:
    return "InternalError";
  }
  return "InternalError";
}
auto IsRecoverable(Failure failure) -> bool {
  return failure >= Failure::Overflow && failure <= Failure::Fractional;
}

auto Value::bits() const -> std::array<std::uint64_t, 2> {
  if (Type == Target::Type::MathematicalInteger) {
    throw std::logic_error("mathematical integers have no machine bit pattern");
  }
  const auto bits = Target::IsFloat(Type)
                        ? Access::Get(*this).Float.bitcastToAPInt()
                        : Access::Get(*this).Integer;
  return {bits.extractBitsAsZExtValue(std::min(64U, bits.getBitWidth()), 0),
          bits.getBitWidth() > 64 ? bits.extractBitsAsZExtValue(64, 64) : 0};
}
auto Value::boolean() const -> bool {
  if (Type != Target::Type::Bool) {
    throw std::logic_error("non-boolean numeric value");
  }
  return !Access::Get(*this).Integer.isZero();
}
auto Value::describe() const -> std::string {
  llvm::SmallString<64> text;
  if (Target::IsFloat(Type)) {
    Access::Get(*this).Float.toString(text);
  } else {
    Access::Get(*this).Integer.toString(
        text, 10,
        Target::IsSigned(Type) || Type == Target::Type::MathematicalInteger);
  }
  return std::string(text.data(), text.size());
}
auto Value::Boolean(bool value) -> Value {
  return Access::Integer(Target::Type::Bool, llvm::APInt(1, value));
}
auto Value::Unit() -> Value {
  return Access::Integer(Target::Type::Unit, llvm::APInt(1, 0));
}
auto Value::FromBits(Target::Type type, std::array<std::uint64_t, 2> words,
                     Target::Description target) -> Value {
  if (!target.valid()) {
    throw std::invalid_argument("unsupported numeric target description");
  }
  const auto width = target.bits(type);
  if (type == Target::Type::Unit) {
    return Unit();
  }
  if (width == 0) {
    throw std::logic_error("type has no scalar bits");
  }
  const llvm::APInt bits(
      width, llvm::ArrayRef<std::uint64_t>(words.data(), width > 64 ? 2 : 1));
  if (Target::IsFloat(type)) {
    return Access::Float(type, llvm::APFloat(Semantics(type), bits));
  }
  return Access::Integer(type, bits);
}

auto Literal(std::string_view digits, unsigned radix, Type type, bool negative,
             Budget &budget, Target::Description target) -> Result {
  if (!target.valid()) {
    return Error(Failure::InternalError);
  }
  if (!budget.consume()) {
    return Error(Failure::ResourceExceeded);
  }
  std::string spelling;
  for (const auto c : digits) {
    if (c != '_') {
      spelling += c;
    }
  }
  const auto count = std::count_if(spelling.begin(), spelling.end(),
                                   [](char c) { return c >= '0' && c <= '9'; });
  const auto digitCount =
      Target::IsFloat(type) ? static_cast<std::size_t>(count) : spelling.size();
  if (spelling.empty() || digitCount > budget.limits().LiteralDigits) {
    return Error(Failure::ResourceExceeded);
  }
  if (Target::IsFloat(type)) {
    llvm::APFloat value(Semantics(type));
    auto status =
        value.convertFromString(spelling, llvm::APFloat::rmNearestTiesToEven);
    if (!status) {
      llvm::consumeError(status.takeError());
      return Error(Failure::InternalError);
    }
    if (value.isInfinity()) {
      return Error(Failure::OutOfRange);
    }
    if (negative) {
      value.changeSign();
    }
    return Success(Access::Float(type, std::move(value)));
  }
  if (!(Target::IsInteger(type) || type == Type::MathematicalInteger) ||
      (radix != 2 && radix != 8 && radix != 10 && radix != 16)) {
    return Error(Failure::InternalError);
  }
  for (char c : spelling) {
    unsigned digit = 16;
    if (c >= '0' && c <= '9') {
      digit = static_cast<unsigned>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = static_cast<unsigned>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      digit = static_cast<unsigned>(c - 'A' + 10);
    }
    if (digit >= radix) {
      return Error(Failure::InternalError);
    }
  }
  // Charge the original spelling above, then give APInt only significant
  // digits. Its string constructor has a length-based width precondition;
  // getBitsNeeded alone does not satisfy it for decimal leading zeroes.
  const auto first = spelling.find_first_not_of('0');
  const auto significant = first == std::string::npos
                               ? llvm::StringRef("0")
                               : llvm::StringRef(spelling).drop_front(first);
  const auto width = llvm::APInt::getBitsNeeded(significant, radix) + 1;
  if (width > budget.limits().IntegerBits) {
    return Error(Failure::ResourceExceeded);
  }
  llvm::APInt value(std::max(width, 2U), significant, radix);
  if (negative) {
    value = -value;
  }
  return FinishInteger(std::move(value), type, budget, target,
                       Failure::OutOfRange);
}

auto Unary(Operation op, const Value &value, Budget &budget,
           Target::Description target) -> Result {
  if (!target.valid()) {
    return Error(Failure::InternalError);
  }
  if (!budget.consume()) {
    return Error(Failure::ResourceExceeded);
  }
  if (op == Operation::Plus) {
    if (Target::IsNumeric(value.type()) ||
        value.type() == Type::MathematicalInteger) {
      return Success(value);
    }
    return Error(Failure::InternalError);
  }
  if (op == Operation::Not && value.type() == Type::Bool) {
    return Success(Value::Boolean(!value.boolean()));
  }
  if (Target::IsFloat(value.type()) && op == Operation::Negate) {
    auto result = Access::Get(value).Float;
    result.changeSign();
    return Success(Access::Float(value.type(), std::move(result)));
  }
  if (op == Operation::Complement && Target::IsInteger(value.type())) {
    return Success(Access::Integer(value.type(), ~Access::Get(value).Integer));
  }
  if (op != Operation::Negate || !(Target::IsInteger(value.type()) ||
                                   value.type() == Type::MathematicalInteger)) {
    return Error(Failure::InternalError);
  }
  auto integer = Mathematics(value);
  if (integer.getBitWidth() >= budget.limits().IntegerBits) {
    return Error(Failure::ResourceExceeded);
  }
  return FinishInteger(-integer.sext(integer.getBitWidth() + 1), value.type(),
                       budget, target, Failure::Overflow);
}

auto Binary(Operation op, const Value &left, const Value &right, Budget &budget,
            Target::Description target) -> Result {
  if (!target.valid()) {
    return Error(Failure::InternalError);
  }
  if (!budget.consume()) {
    return Error(Failure::ResourceExceeded);
  }
  if (left.type() == Type::Bool && right.type() == Type::Bool) {
    if (op == Operation::LogicalAnd || op == Operation::LogicalOr) {
      return Success(Value::Boolean(op == Operation::LogicalAnd
                                        ? left.boolean() && right.boolean()
                                        : left.boolean() || right.boolean()));
    }
    if (op == Operation::Equal || op == Operation::NotEqual) {
      return Success(Value::Boolean((left.boolean() == right.boolean()) ==
                                    (op == Operation::Equal)));
    }
    return Error(Failure::InternalError);
  }
  if (Target::IsFloat(left.type()) && left.type() == right.type()) {
    auto value = Access::Get(left).Float;
    const auto &rhs = Access::Get(right).Float;
    if (IsComparison(op)) {
      const auto comparison = value.compare(rhs);
      if (comparison == llvm::APFloat::cmpUnordered) {
        return Success(Value::Boolean(op == Operation::NotEqual));
      }
      const auto ordering = comparison == llvm::APFloat::cmpLessThan      ? -1
                            : comparison == llvm::APFloat::cmpGreaterThan ? 1
                                                                          : 0;
      return Success(Value::Boolean(Comparison(op, ordering)));
    }
    switch (op) {
    case Operation::Add:
      value.add(rhs, llvm::APFloat::rmNearestTiesToEven);
      break;
    case Operation::Subtract:
      value.subtract(rhs, llvm::APFloat::rmNearestTiesToEven);
      break;
    case Operation::Multiply:
      value.multiply(rhs, llvm::APFloat::rmNearestTiesToEven);
      break;
    case Operation::Divide:
      value.divide(rhs, llvm::APFloat::rmNearestTiesToEven);
      break;
    default:
      return Error(Failure::InternalError);
    }
    return Success(Access::Float(left.type(), std::move(value)));
  }
  if (!(Target::IsInteger(left.type()) ||
        left.type() == Type::MathematicalInteger) ||
      !(Target::IsInteger(right.type()) ||
        right.type() == Type::MathematicalInteger)) {
    return Error(Failure::InternalError);
  }
  if (op == Operation::ShiftLeft || op == Operation::ShiftRight) {
    const auto count = Mathematics(right);
    const auto width = target.bits(left.type());
    if (width == 0 || count.isNegative() || count.getLimitedValue() >= width) {
      return Error(Failure::ShiftCount);
    }
    const auto amount = static_cast<unsigned>(count.getZExtValue());
    const auto &bits = Access::Get(left).Integer;
    if (op == Operation::ShiftLeft) {
      return Success(Access::Integer(left.type(), bits.shl(amount)));
    }
    auto result =
        Target::IsSigned(left.type()) ? bits.ashr(amount) : bits.lshr(amount);
    return Success(Access::Integer(left.type(), std::move(result)));
  }
  if (left.type() != right.type()) {
    return Error(Failure::InternalError);
  }
  if (op == Operation::BitAnd || op == Operation::BitOr ||
      op == Operation::BitXor) {
    const auto &lhs = Access::Get(left).Integer;
    const auto &rhs = Access::Get(right).Integer;
    if (!Target::IsInteger(left.type())) {
      return Error(Failure::InternalError);
    }
    if (op == Operation::BitAnd) {
      return Success(Access::Integer(left.type(), lhs & rhs));
    }
    if (op == Operation::BitOr) {
      return Success(Access::Integer(left.type(), lhs | rhs));
    }
    return Success(Access::Integer(left.type(), lhs ^ rhs));
  }
  auto lhs = Mathematics(left);
  auto rhs = Mathematics(right);
  const auto width = op == Operation::Multiply
                         ? lhs.getBitWidth() + rhs.getBitWidth()
                         : std::max(lhs.getBitWidth(), rhs.getBitWidth()) + 1;
  if (width > budget.limits().IntegerBits) {
    return Error(Failure::ResourceExceeded);
  }
  lhs = lhs.sext(width);
  rhs = rhs.sext(width);
  if (IsComparison(op)) {
    const auto ordering = lhs.slt(rhs) ? -1 : lhs.sgt(rhs) ? 1 : 0;
    return Success(Value::Boolean(Comparison(op, ordering)));
  }
  llvm::APInt result(width, 0);
  switch (op) {
  case Operation::Add:
    result = lhs + rhs;
    break;
  case Operation::Subtract:
    result = lhs - rhs;
    break;
  case Operation::Multiply:
    result = lhs * rhs;
    break;
  case Operation::Divide:
  case Operation::Remainder:
    if (rhs.isZero()) {
      return Error(Failure::DivisionByZero);
    }
    result = op == Operation::Divide ? lhs.sdiv(rhs) : lhs.srem(rhs);
    break;
  default:
    return Error(Failure::InternalError);
  }
  return FinishInteger(std::move(result), left.type(), budget, target,
                       Failure::Overflow);
}

auto Convert(const Value &source, Type targetType, Budget &budget,
             Target::Description target) -> Result {
  if (!target.valid()) {
    return Error(Failure::InternalError);
  }
  if (!budget.consume()) {
    return Error(Failure::ResourceExceeded);
  }
  const auto sourceType = source.type();
  const bool integer =
      Target::IsInteger(sourceType) || sourceType == Type::MathematicalInteger;
  if ((!integer && !Target::IsFloat(sourceType)) ||
      !Target::IsNumeric(targetType)) {
    return Error(Failure::InternalError);
  }
  if (integer && Target::IsInteger(targetType)) {
    return FinishInteger(Mathematics(source), targetType, budget, target,
                         Failure::OutOfRange);
  }
  if (integer) {
    return IntegerToFloat(source, targetType, budget);
  }
  if (Target::IsInteger(targetType)) {
    return FloatToInteger(source, targetType, budget, target);
  }
  auto value = Access::Get(source).Float;
  if (sourceType == targetType) {
    return Success(source);
  }
  if (value.isFinite() && sourceType == Type::F64 && targetType == Type::F32) {
    auto maximum = llvm::APFloat::getLargest(llvm::APFloat::IEEEsingle());
    bool ignored = false;
    maximum.convert(llvm::APFloat::IEEEdouble(),
                    llvm::APFloat::rmNearestTiesToEven, &ignored);
    auto magnitude = value;
    magnitude.clearSign();
    if (magnitude.compare(maximum) == llvm::APFloat::cmpGreaterThan) {
      return Error(Failure::OutOfRange);
    }
  }
  const bool finite = value.isFinite();
  bool losesInformation = false;
  value.convert(Semantics(targetType), llvm::APFloat::rmNearestTiesToEven,
                &losesInformation);
  if (finite && losesInformation) {
    return Error(Failure::PrecisionLoss);
  }
  return Success(Access::Float(targetType, std::move(value)));
}
} // namespace Chtholly::Numeric
