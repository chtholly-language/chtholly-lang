// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <cstdint>

namespace Chtholly::Runtime {
inline constexpr unsigned ABIVersion = 3;
inline constexpr unsigned FailureExit = 120;
inline constexpr unsigned AbortExit = 121;
struct Bits {
  std::uint64_t Low;
  std::uint64_t High;
};
enum class ScalarCode : std::uint32_t {
  I8 = 1,
  I16 = 2,
  I32 = 3,
  I64 = 4,
  I128 = 5,
  U8 = 6,
  U16 = 7,
  U32 = 8,
  U64 = 9,
  U128 = 10,
  ISize = 11,
  USize = 12,
  F32 = 13,
  F64 = 14
};
enum class CheckedOperation : std::uint32_t {
  Negate = 1,
  Add = 4,
  Subtract = 5,
  Multiply = 6,
  Divide = 7,
  Remainder = 8,
  ShiftLeft = 9,
  ShiftRight = 10,
  Convert = 22
};
enum class FailureCode : std::uint32_t {
  None = 0,
  Overflow = 1,
  DivisionByZero = 2,
  ShiftCount = 3,
  OutOfRange = 4,
  PrecisionLoss = 5,
  NonFinite = 6,
  Fractional = 7,
  Panic = 20,
  Assertion = 21,
  IndexOutOfBounds = 22
};
extern "C" {
auto chtholly_runtime_abi_v3() -> void;
auto chtholly_runtime_initialize() -> void;
auto chtholly_runtime_checked(std::uint32_t operation, std::uint32_t sourceType,
                              std::uint32_t resultType, std::uint32_t rightType,
                              std::uint64_t leftLow, std::uint64_t leftHigh,
                              std::uint64_t rightLow, std::uint64_t rightHigh,
                              Bits *result) -> std::uint32_t;
[[noreturn]] auto chtholly_runtime_fail(std::uint32_t category,
                                        const char *file, std::uint64_t line,
                                        std::uint64_t column) -> void;
[[noreturn]] auto chtholly_runtime_abort() -> void;
}
} // namespace Chtholly::Runtime
