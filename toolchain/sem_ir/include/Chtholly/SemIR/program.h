// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <cstdint>

namespace Chtholly::Check {
class Checker;
}

namespace Chtholly::SemIR {

// A checked, parameterless native main returning an i32 decimal constant.
// Construction is restricted to Check; CodeGen never reparses source text.
class Program {
public:
  auto returnValue() const -> std::int32_t { return ReturnValue; }

private:
  friend class Check::Checker;
  explicit Program(std::int32_t value) : ReturnValue(value) {}
  std::int32_t ReturnValue;
};

} // namespace Chtholly::SemIR
