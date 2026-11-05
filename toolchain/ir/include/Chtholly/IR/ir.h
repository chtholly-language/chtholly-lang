// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/Numeric/numeric.h>
#include <Chtholly/Source/source.h>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Chtholly::Diagnostic {
class Diagnostics;
}
namespace Chtholly::IR {
using Id = std::uint32_t;
inline constexpr Id None = std::numeric_limits<Id>::max();
using Type = Target::Type;
enum class Opcode { Constant, Load, Store, Unary, Binary, Call };
struct Instruction {
  Opcode Code = Opcode::Constant;
  Id Result = None;
  Id Entity = None;
  Numeric::Operation Operation = Numeric::Operation::Plus;
  std::optional<Numeric::Value> Constant;
  std::vector<Id> Operands;
};
struct Edge {
  Id Target = None;
  std::vector<Id> Arguments;
};
enum class Exit {
  Missing,
  Jump,
  Branch,
  Checked,
  Return,
  Fail,
  Abort,
  Unreachable
};
struct Terminator {
  Exit Kind = Exit::Missing;
  std::vector<Id> Operands;
  Edge First;
  Edge Second;
  Numeric::Operation Operation = Numeric::Operation::Plus;
  bool Conversion = false;
  Type ResultType = Type::Invalid;
  Source::Location Location{};
};
struct Block {
  std::vector<Id> Parameters;
  std::vector<Instruction> Instructions;
  Terminator End;
};
struct Parameter {
  Type ValueType;
  Id Local;
};
struct Function {
  std::string Name;
  Type ReturnType = Type::Unit;
  std::vector<Parameter> Parameters;
  std::vector<Type> Locals;
  std::vector<Type> Values;
  std::vector<Block> Blocks;
  bool HasNormalEntry = true;
};
// Draft records are mutable by producers and damage fixtures. Only Verify can
// turn an owning draft into the immutable object accepted by CodeGen.
struct Draft {
  Target::Description Target = Target::WindowsX64;
  std::string SourceName;
  std::vector<Function> Functions;
  Id Entry = None;
};
class Program {
public:
  auto storage() const -> const Draft & { return *Data; }

private:
  friend auto Verify(Draft,
                     Diagnostic::Diagnostics &) -> std::optional<Program>;
  explicit Program(Draft data)
      : Data(std::make_shared<const Draft>(std::move(data))) {}
  std::shared_ptr<const Draft> Data;
};
auto Verify(Draft draft,
            Diagnostic::Diagnostics &diagnostics) -> std::optional<Program>;
} // namespace Chtholly::IR
