// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once

#include <Chtholly/Target/target.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Chtholly::Numeric {

enum class Failure : std::uint32_t {
  None,
  Overflow,
  DivisionByZero,
  ShiftCount,
  OutOfRange,
  PrecisionLoss,
  NonFinite,
  Fractional,
  ResourceExceeded,
  InternalError
};
enum class Operation {
  Plus,
  Negate,
  Complement,
  Not,
  Add,
  Subtract,
  Multiply,
  Divide,
  Remainder,
  ShiftLeft,
  ShiftRight,
  BitAnd,
  BitXor,
  BitOr,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Equal,
  NotEqual,
  LogicalAnd,
  LogicalOr
};

struct Limits {
  std::size_t LiteralDigits = 4096;
  unsigned IntegerBits = 65536;
  std::size_t EvaluationSteps = 100000;
};

class Budget {
public:
  explicit Budget(Limits limits = {}) : Boundaries(limits) {}
  auto consume() -> bool { return Steps++ < Boundaries.EvaluationSteps; }
  auto limits() const -> const Limits & { return Boundaries; }

private:
  Limits Boundaries;
  std::size_t Steps = 0;
};

// Immutable values own their private implementation. Typed integer bits and
// mathematical integer magnitude are different representations and APIs.
class Value {
public:
  struct Storage;
  Value() = default;
  auto type() const -> Target::Type { return Type; }
  auto bits() const -> std::array<std::uint64_t, 2>;
  auto boolean() const -> bool;
  auto describe() const -> std::string;
  static auto Boolean(bool value) -> Value;
  static auto Unit() -> Value;
  static auto
  FromBits(Target::Type type, std::array<std::uint64_t, 2> bits,
           Target::Description target = Target::WindowsX64) -> Value;

private:
  friend struct Access;
  Value(Target::Type type, std::shared_ptr<const Storage> storage)
      : Type(type), Data(std::move(storage)) {}
  Target::Type Type = Target::Type::Invalid;
  std::shared_ptr<const Storage> Data;
};

struct Result {
  std::optional<Value> ValueResult;
  Failure Error = Failure::None;
  explicit operator bool() const { return ValueResult.has_value(); }
};

auto FailureName(Failure failure) -> std::string_view;
auto IsRecoverable(Failure failure) -> bool;
auto Literal(std::string_view digits, unsigned radix, Target::Type type,
             bool negative, Budget &budget,
             Target::Description target = Target::WindowsX64) -> Result;
auto Unary(Operation op, const Value &value, Budget &budget,
           Target::Description target = Target::WindowsX64) -> Result;
auto Binary(Operation op, const Value &left, const Value &right, Budget &budget,
            Target::Description target = Target::WindowsX64) -> Result;
auto Convert(const Value &source, Target::Type targetType, Budget &budget,
             Target::Description target = Target::WindowsX64) -> Result;

} // namespace Chtholly::Numeric
