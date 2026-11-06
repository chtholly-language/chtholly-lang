// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Runtime/abi.h>
#include <Chtholly/Target/target.h>

#include <bit>
#include <charconv>
#include <cstring>
#include <immintrin.h>
#include <windows.h>

namespace Chtholly::Runtime {
using Type = Target::Scalar;
using U128 = unsigned __int128;
using I128 = __int128;

extern "C" {
I128 __divti3(I128, I128);
I128 __modti3(I128, I128);
U128 __udivti3(U128, U128);
U128 __umodti3(U128, U128);
float __floattisf(I128);
double __floattidf(I128);
float __floatuntisf(U128);
double __floatuntidf(U128);
}
namespace {
using Error = FailureCode;
using Op = CheckedOperation;
auto DecodeScalar(std::uint32_t code) -> Type {
  switch (static_cast<ScalarCode>(code)) {
  case ScalarCode::I8:
    return Type::I8;
  case ScalarCode::I16:
    return Type::I16;
  case ScalarCode::I32:
    return Type::I32;
  case ScalarCode::I64:
    return Type::I64;
  case ScalarCode::I128:
    return Type::I128;
  case ScalarCode::U8:
    return Type::U8;
  case ScalarCode::U16:
    return Type::U16;
  case ScalarCode::U32:
    return Type::U32;
  case ScalarCode::U64:
    return Type::U64;
  case ScalarCode::U128:
    return Type::U128;
  case ScalarCode::ISize:
    return Type::ISize;
  case ScalarCode::USize:
    return Type::USize;
  case ScalarCode::F32:
    return Type::F32;
  case ScalarCode::F64:
    return Type::F64;
  }
  return Type::Invalid;
}
auto Width(Type type) -> unsigned { return Target::WindowsX64.bits(type); }
auto Signed(Type type) -> bool {
  return (type >= Type::I8 && type <= Type::I128) || type == Type::ISize;
}
auto Integer(Type type) -> bool {
  return type >= Type::I8 && type <= Type::USize;
}
auto Float(Type type) -> bool { return type == Type::F32 || type == Type::F64; }
auto Mask(unsigned width) -> U128 {
  return width == 128 ? ~U128{0} : (U128{1} << width) - 1;
}
auto Pack(std::uint64_t low, std::uint64_t high) -> U128 {
  return U128{low} | (U128{high} << 64);
}
auto Store(U128 value, Bits *output) -> void {
  output->Low = static_cast<std::uint64_t>(value);
  output->High = static_cast<std::uint64_t>(value >> 64);
}
auto SignExtend(U128 value, unsigned width) -> I128 {
  value &= Mask(width);
  if (width < 128 && (value & (U128{1} << (width - 1))) != 0) {
    value |= ~Mask(width);
  }
  return std::bit_cast<I128>(value);
}
auto Magnitude(U128 value, Type type, bool &negative) -> U128 {
  value &= Mask(Width(type));
  negative = Signed(type) && (value & (U128{1} << (Width(type) - 1))) != 0;
  return negative ? (U128{0} - value) & Mask(Width(type)) : value;
}
auto Fits(U128 magnitude, bool negative, Type type) -> bool {
  const auto width = Width(type);
  if (Signed(type)) {
    const auto bound = U128{1} << (width - 1);
    return negative ? magnitude <= bound : magnitude < bound;
  }
  return (!negative || magnitude == 0) && magnitude <= Mask(width);
}
auto BitLength(U128 value) -> unsigned {
  if (value == 0) {
    return 0;
  }
  const auto high = static_cast<std::uint64_t>(value >> 64);
  return high != 0 ? 128U - static_cast<unsigned>(std::countl_zero(high))
                   : 64U - static_cast<unsigned>(std::countl_zero(
                               static_cast<std::uint64_t>(value)));
}
auto FloatInteger(U128 raw, Type source, Type destination,
                  Bits *output) -> Error {
  const auto bits = static_cast<std::uint64_t>(raw);
  const unsigned fractionBits = source == Type::F32 ? 23 : 52;
  const unsigned exponentBits = source == Type::F32 ? 8 : 11;
  const auto exponentMask = (std::uint64_t{1} << exponentBits) - 1;
  const auto fractionMask = (std::uint64_t{1} << fractionBits) - 1;
  const auto exponentField = (bits >> fractionBits) & exponentMask;
  const auto fraction = bits & fractionMask;
  const bool negative = ((bits >> (fractionBits + exponentBits)) & 1) != 0;
  if (exponentField == exponentMask) {
    return Error::NonFinite;
  }
  if (exponentField == 0 && fraction == 0) {
    Store(0, output);
    return Error::None;
  }
  const auto exponent =
      static_cast<int>(exponentField) - (source == Type::F32 ? 127 : 1023);
  if (exponentField == 0 || exponent < 0) {
    return Error::Fractional;
  }
  U128 magnitude = fraction | (std::uint64_t{1} << fractionBits);
  if (exponent < static_cast<int>(fractionBits)) {
    const auto discard = fractionBits - static_cast<unsigned>(exponent);
    if ((magnitude & Mask(discard)) != 0) {
      return Error::Fractional;
    }
    magnitude >>= discard;
  } else {
    if (exponent >= 128) {
      return Error::OutOfRange;
    }
    magnitude <<= static_cast<unsigned>(exponent) - fractionBits;
  }
  if (!Fits(magnitude, negative, destination)) {
    return Error::OutOfRange;
  }
  Store((negative ? U128{0} - magnitude : magnitude) & Mask(Width(destination)),
        output);
  return Error::None;
}
auto Convert(U128 raw, Type source, Type destination, Bits *output) -> Error {
  if (Integer(source)) {
    bool negative = false;
    const auto magnitude = Magnitude(raw, source, negative);
    if (Integer(destination)) {
      if (!Fits(magnitude, negative, destination)) {
        return Error::OutOfRange;
      }
      Store((negative ? U128{0} - magnitude : magnitude) &
                Mask(Width(destination)),
            output);
      return Error::None;
    }
    const auto maximumF32 = ((U128{1} << 24) - 1) << 104;
    if (destination == Type::F32 && magnitude > maximumF32) {
      return Error::OutOfRange;
    }
    const auto precision = destination == Type::F32 ? 24U : 53U;
    const auto length = BitLength(magnitude);
    if (length > precision && (magnitude & Mask(length - precision)) != 0) {
      return Error::PrecisionLoss;
    }
    if (destination == Type::F32) {
      const auto result = Signed(source)
                              ? __floattisf(SignExtend(raw, Width(source)))
                              : __floatuntisf(raw & Mask(Width(source)));
      Store(std::bit_cast<std::uint32_t>(result), output);
    } else {
      const auto result = Signed(source)
                              ? __floattidf(SignExtend(raw, Width(source)))
                              : __floatuntidf(raw & Mask(Width(source)));
      Store(std::bit_cast<std::uint64_t>(result), output);
    }
    return Error::None;
  }
  if (Integer(destination)) {
    return FloatInteger(raw, source, destination, output);
  }
  if (source == destination) {
    Store(raw & Mask(Width(source)), output);
    return Error::None;
  }
  if (source == Type::F32) {
    const auto input = std::bit_cast<float>(static_cast<std::uint32_t>(raw));
    Store(std::bit_cast<std::uint64_t>(static_cast<double>(input)), output);
    return Error::None;
  }
  const auto bits = static_cast<std::uint64_t>(raw);
  const auto absolute = bits & 0x7fffffffffffffffULL;
  const bool finite = absolute < 0x7ff0000000000000ULL;
  if (finite && absolute > 0x47efffffe0000000ULL) {
    return Error::OutOfRange;
  }
  const auto input = std::bit_cast<double>(bits);
  const auto result = static_cast<float>(input);
  if (finite && static_cast<double>(result) != input) {
    return Error::PrecisionLoss;
  }
  Store(std::bit_cast<std::uint32_t>(result), output);
  return Error::None;
}
auto Arithmetic(Op op, Type source, Type rightType, U128 left, U128 right,
                Bits *output) -> Error {
  const auto width = Width(source);
  left &= Mask(width);
  if (op == Op::ShiftLeft || op == Op::ShiftRight) {
    bool negative = false;
    const auto count = Magnitude(right, rightType, negative);
    if (negative || count >= width) {
      return Error::ShiftCount;
    }
    const auto amount = static_cast<unsigned>(count);
    const auto result =
        op == Op::ShiftLeft ? left << amount
        : Signed(source)
            ? std::bit_cast<U128>(SignExtend(left, width) >> amount)
            : left >> amount;
    Store(result & Mask(width), output);
    return Error::None;
  }
  right &= Mask(width);
  if (Signed(source)) {
    const auto lhs = SignExtend(left, width);
    const auto rhs = SignExtend(right, width);
    I128 result = 0;
    bool overflow = false;
    switch (op) {
    case Op::Add:
      overflow = __builtin_add_overflow(lhs, rhs, &result);
      break;
    case Op::Subtract:
      overflow = __builtin_sub_overflow(lhs, rhs, &result);
      break;
    case Op::Multiply:
      overflow = __builtin_mul_overflow(lhs, rhs, &result);
      break;
    case Op::Negate:
      overflow = __builtin_sub_overflow(I128{0}, lhs, &result);
      break;
    case Op::Divide:
    case Op::Remainder: {
      if (rhs == 0) {
        return Error::DivisionByZero;
      }
      const auto minimum = SignExtend(U128{1} << (width - 1), width);
      if (lhs == minimum && rhs == -1) {
        if (op == Op::Divide) {
          return Error::Overflow;
        }
        Store(0, output);
        return Error::None;
      }
      result = op == Op::Divide ? __divti3(lhs, rhs) : __modti3(lhs, rhs);
      break;
    }
    default:
      ExitProcess(122);
    }
    if (overflow) {
      return Error::Overflow;
    }
    const auto raw = std::bit_cast<U128>(result);
    const auto negative = result < 0;
    if (!Fits(negative ? U128{0} - raw : raw, negative, source)) {
      return Error::Overflow;
    }
    Store(raw & Mask(width), output);
    return Error::None;
  }
  U128 result = 0;
  bool overflow = false;
  switch (op) {
  case Op::Add:
    overflow = __builtin_add_overflow(left, right, &result);
    break;
  case Op::Subtract:
    overflow = __builtin_sub_overflow(left, right, &result);
    break;
  case Op::Multiply:
    overflow = __builtin_mul_overflow(left, right, &result);
    break;
  case Op::Negate:
    overflow = left != 0;
    break;
  case Op::Divide:
  case Op::Remainder:
    if (right == 0) {
      return Error::DivisionByZero;
    }
    result = op == Op::Divide ? __udivti3(left, right) : __umodti3(left, right);
    break;
  default:
    ExitProcess(122);
  }
  if (overflow || result > Mask(width)) {
    return Error::Overflow;
  }
  Store(result, output);
  return Error::None;
}
auto Write(const char *bytes, std::size_t length) -> void {
  const auto handle = GetStdHandle(STD_ERROR_HANDLE);
  while (length != 0) {
    DWORD written = 0;
    const auto chunk =
        static_cast<DWORD>(length > 0x7fffffff ? 0x7fffffff : length);
    if (!WriteFile(handle, bytes, chunk, &written, nullptr) || written == 0) {
      return;
    }
    bytes += written;
    length -= written;
  }
}
} // namespace
extern "C" auto chtholly_runtime_abi_v3() -> void {}
extern "C" auto chtholly_runtime_initialize() -> void {
  // nearest ties-even, masked exceptions, gradual underflow, cleared flags.
  _mm_setcsr(0x1f80);
}
extern "C" auto
chtholly_runtime_checked(std::uint32_t operation, std::uint32_t sourceType,
                         std::uint32_t resultType, std::uint32_t rightType,
                         std::uint64_t leftLow, std::uint64_t leftHigh,
                         std::uint64_t rightLow, std::uint64_t rightHigh,
                         Bits *result) -> std::uint32_t {
  const auto source = DecodeScalar(sourceType);
  const auto destination = DecodeScalar(resultType);
  const auto op = static_cast<Op>(operation);
  if (result == nullptr ||
      (op == Op::Convert ? (!Integer(source) && !Float(source)) ||
                               (!Integer(destination) && !Float(destination))
                         : !Integer(source) || source != destination ||
                               !Integer(DecodeScalar(rightType)))) {
    ExitProcess(122);
  }
  return static_cast<std::uint32_t>(
      op == Op::Convert
          ? Convert(Pack(leftLow, leftHigh), source, destination, result)
          : Arithmetic(op, source, DecodeScalar(rightType),
                       Pack(leftLow, leftHigh), Pack(rightLow, rightHigh),
                       result));
}
extern "C" [[noreturn]] auto
chtholly_runtime_fail(std::uint32_t category, const char *file,
                      std::uint64_t line, std::uint64_t column) -> void {
  const char *name;
  switch (category) {
  case 1:
    name = "Arithmetic.Overflow";
    break;
  case 2:
    name = "Arithmetic.DivisionByZero";
    break;
  case 3:
    name = "Arithmetic.ShiftCount";
    break;
  case 4:
    name = "Conversion.OutOfRange";
    break;
  case 5:
    name = "Conversion.PrecisionLoss";
    break;
  case 6:
    name = "Conversion.NonFinite";
    break;
  case 7:
    name = "Conversion.Fractional";
    break;
  case 20:
    name = "Panic";
    break;
  case 21:
    name = "Assertion";
    break;
  case static_cast<unsigned>(Error::IndexOutOfBounds):
    name = "Index.OutOfBounds";
    break;
  default:
    ExitProcess(122);
  }
  Write(file, std::strlen(file));
  char position[64];
  auto *cursor = position;
  *cursor++ = ':';
  cursor = std::to_chars(cursor, position + sizeof(position), line).ptr;
  *cursor++ = ':';
  cursor = std::to_chars(cursor, position + sizeof(position), column).ptr;
  constexpr char prefix[] = ": chtholly failure[";
  Write(position, static_cast<std::size_t>(cursor - position));
  Write(prefix, sizeof(prefix) - 1);
  Write(name, std::strlen(name));
  Write("]\n", 2);
  ExitProcess(FailureExit);
}
extern "C" [[noreturn]] auto chtholly_runtime_abort() -> void {
  ExitProcess(AbortExit);
}
} // namespace Chtholly::Runtime
