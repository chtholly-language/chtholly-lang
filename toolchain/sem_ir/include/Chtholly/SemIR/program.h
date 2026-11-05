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

namespace Chtholly::Check {
class Checker;
}
namespace Chtholly::SemIR {
using Id = std::uint32_t;
inline constexpr Id None = std::numeric_limits<Id>::max();
using Type = Target::Type;
enum class Kind {
  Literal,
  Local,
  Group,
  Unary,
  Binary,
  Convert,
  Call,
  Block,
  If,
  Bind,
  Assign,
  Discard,
  Return
};
enum class Builtin { None, Panic, Abort, Assert };
struct Node {
  Kind Form = Kind::Literal;
  Source::Span Span{};
  Source::Location Location{};
  Type ValueType = Type::Invalid;
  bool Completes = true;
  bool Executable = true;
  Numeric::Operation Operation = Numeric::Operation::Plus;
  std::optional<Numeric::Value> Constant;
  Numeric::Failure ConstantFailure = Numeric::Failure::None;
  Id Left = None;
  Id Right = None;
  Id Alternative = None;
  Id Entity = None;
  Builtin Intrinsic = Builtin::None;
  std::vector<Id> Statements;
  std::vector<Id> Arguments;
  // Arguments retain source order; Slots maps each argument to a parameter.
  std::vector<Id> Slots;
};
struct Local {
  std::string Name;
  Type ValueType = Type::Invalid;
  bool Writable = false;
};
struct Parameter {
  std::string Label;
  Id Local = None;
  Type ValueType = Type::Invalid;
};
struct Function {
  std::string Name;
  Type ReturnType = Type::Unit;
  std::vector<Parameter> Parameters;
  std::vector<Local> Locals;
  Id Body = None;
  bool HasNormalEntry = true;
};
struct Storage {
  Target::Description Target = Target::WindowsX64;
  std::string SourceName;
  std::vector<Node> Nodes;
  std::vector<Function> Functions;
  Id Entry = None;
};
class Program {
public:
  auto storage() const -> const Storage & { return *Data; }

private:
  friend class Check::Checker;
  explicit Program(Storage data)
      : Data(std::make_shared<const Storage>(std::move(data))) {}
  std::shared_ptr<const Storage> Data;
};
} // namespace Chtholly::SemIR
