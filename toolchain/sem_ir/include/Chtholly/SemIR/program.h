// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/Numeric/numeric.h>
#include <Chtholly/Source/source.h>
#include <Chtholly/Types/types.h>
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
using Type = Types::TypeId;
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
  Return,
  Construct,
  Project,
  Index,
  Length,
  Copy,
  Move,
  Match,
  While,
  Loop,
  Break,
  Continue
};
enum class Category { Value, Place, TrialPlace };
enum class PatternKind {
  Binding,
  Wildcard,
  Unit,
  Product,
  Variant,
  Boolean,
  Integer,
  Or
};
struct Pattern {
  PatternKind Form = PatternKind::Wildcard;
  Type ValueType;
  Source::Location Location{};
  Id Local = None;
  Id Variant = None;
  std::optional<Numeric::Value> Literal;
  std::vector<Id> Children;
  std::vector<std::uint64_t> Fields;
};
struct Arm {
  Id Pattern = None;
  Id Guard = None;
  Id Body = None;
  bool Reachable = true;
  bool BodyReachable = true;
};
enum class Builtin { None, Panic, Abort, Assert };
struct Node {
  Kind Form = Kind::Literal;
  Source::Span Span{};
  Source::Location Location{};
  Type ValueType = Type::Invalid;
  bool Completes = true;
  bool Executable = true;
  Category ValueCategory = Category::Value;
  bool Writable = false;
  bool Repeat = false;
  std::optional<std::uint64_t> FixedIndex;
  std::uint64_t Count = 0;
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
  std::vector<Arm> Arms;
  Id Pattern = None;
};
struct Local {
  std::string Name;
  Type ValueType = Type::Invalid;
  bool Writable = false;
  bool Trial = false;
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
  std::vector<Pattern> Patterns;
  std::optional<Types::Table> Types;
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
