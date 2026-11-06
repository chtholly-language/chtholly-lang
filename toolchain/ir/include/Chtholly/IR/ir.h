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

namespace Chtholly::Diagnostic {
class Diagnostics;
}
namespace Chtholly::IR {
using Id = std::uint32_t;
inline constexpr Id None = std::numeric_limits<Id>::max();
using Type = Types::TypeId;
enum class Opcode {
  Constant,
  Load,
  Move,
  Observe,
  Store,
  Unary,
  Binary,
  Call,
  Construct,
  Repeat,
  Live,
  Dead,
  Protect,
  Unprotect,
  Locate,
  EndValue,
  EndBindings
};
enum class Projection { Root, Field, Element, DynamicElement, VariantField };
struct Place {
  Type ValueType;
  Id Parent = None;
  Id Local = None;
  Projection Form = Projection::Root;
  std::uint64_t Index = 0;
  Id Variant = None;
  Id IndexValue = None;
  bool Writable = false;
  Id ValueOwner = None;
};
struct Instruction {
  Opcode Code = Opcode::Constant;
  Id Result = None;
  Id Entity = None;
  std::uint64_t Count = 0;
  std::vector<Id> Slots;
  std::size_t SourceOffset = 0;
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
  Unreachable,
  Bounds,
  Variant
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
  Id Place = None;
  Id Variant = None;
  std::uint64_t Count = 0;
  std::size_t SourceOffset = 0;
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
struct BindingGroup {
  // The selected alternative determines creation order and therefore the
  // reverse order in which remaining binding responsibilities end.
  std::vector<std::vector<Id>> CreationOrders;
};
struct Function {
  std::string Name;
  Type ReturnType = Type::Unit;
  std::vector<Parameter> Parameters;
  std::vector<Type> Locals;
  std::vector<Type> Values;
  std::vector<Place> Places;
  std::vector<Id> ValuePlaces;
  std::vector<bool> LocalWritable;
  std::vector<Id> LocalRoots;
  std::vector<BindingGroup> BindingGroups;
  std::vector<Block> Blocks;
  bool HasNormalEntry = true;
};
// Draft records are mutable by producers and damage fixtures. Only Verify can
// turn an owning draft into the immutable object accepted by CodeGen.
struct Draft {
  Target::Description Target = Target::WindowsX64;
  std::string SourceName;
  std::vector<Function> Functions;
  std::optional<Types::Table> Types;
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
