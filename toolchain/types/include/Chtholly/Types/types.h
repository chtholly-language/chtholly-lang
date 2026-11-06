// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Chtholly::Types {

// Primitive describes a built-in operation category, never a declaration ID.
enum class Primitive : std::uint8_t {
  Invalid,
  I8,
  I16,
  I32,
  I64,
  I128,
  U8,
  U16,
  U32,
  U64,
  U128,
  ISize,
  USize,
  F32,
  F64,
  Bool,
  Unit,
  Never
};

class Builder;
class Table;
class TypeId {
public:
  constexpr TypeId() = default;
  constexpr TypeId(Primitive primitive)
      : Index(static_cast<std::uint32_t>(primitive)) {}
  constexpr auto operator==(const TypeId &) const -> bool = default;
  constexpr auto primitive() const -> Primitive {
    return Owner == nullptr && Index <= static_cast<unsigned>(Primitive::Never)
               ? static_cast<Primitive>(Index)
               : Primitive::Invalid;
  }
  constexpr auto index() const -> std::uint32_t { return Index; }
  constexpr auto owner() const -> const void * { return Owner; }
  static const TypeId Invalid, I8, I16, I32, I64, I128, U8, U16, U32, U64, U128,
      ISize, USize, F32, F64, Bool, Unit, Never;

private:
  friend class Builder;
  friend class Table;
  constexpr TypeId(std::uint32_t index, const void *owner)
      : Index(index), Owner(owner) {}
  std::uint32_t Index = 0;
  const void *Owner = nullptr;
};

inline constexpr TypeId TypeId::Invalid{Primitive::Invalid};
inline constexpr TypeId TypeId::I8{Primitive::I8};
inline constexpr TypeId TypeId::I16{Primitive::I16};
inline constexpr TypeId TypeId::I32{Primitive::I32};
inline constexpr TypeId TypeId::I64{Primitive::I64};
inline constexpr TypeId TypeId::I128{Primitive::I128};
inline constexpr TypeId TypeId::U8{Primitive::U8};
inline constexpr TypeId TypeId::U16{Primitive::U16};
inline constexpr TypeId TypeId::U32{Primitive::U32};
inline constexpr TypeId TypeId::U64{Primitive::U64};
inline constexpr TypeId TypeId::U128{Primitive::U128};
inline constexpr TypeId TypeId::ISize{Primitive::ISize};
inline constexpr TypeId TypeId::USize{Primitive::USize};
inline constexpr TypeId TypeId::F32{Primitive::F32};
inline constexpr TypeId TypeId::F64{Primitive::F64};
inline constexpr TypeId TypeId::Bool{Primitive::Bool};
inline constexpr TypeId TypeId::Unit{Primitive::Unit};
inline constexpr TypeId TypeId::Never{Primitive::Never};

struct FieldId {
  TypeId Owner;
  std::uint32_t Index = 0;
  auto operator==(const FieldId &) const -> bool = default;
};
struct VariantId {
  TypeId Owner;
  std::uint32_t Index = 0;
  auto operator==(const VariantId &) const -> bool = default;
};
enum class Kind { Struct, Enum, Tuple, Array };
struct Field {
  std::string Name;
  TypeId Type;
};
struct Variant {
  std::string Name;
  std::vector<Field> Fields;
  bool Inhabited = false;
};
struct Record {
  Kind Form = Kind::Struct;
  std::string Name;
  std::vector<Field> Fields;
  std::vector<Variant> Variants;
  TypeId Element;
  std::uint64_t Length = 0;
  bool Inhabited = false;
  bool Copy = false;
};
enum class ErrorKind {
  Incomplete,
  ForeignIdentity,
  ByValueCycle,
  InvalidCopy,
  InvalidDefinition
};
struct Error {
  ErrorKind Kind;
  TypeId Type;
  std::string Message;
};

// The table owns completed records. IDs from another table are never accepted.
class Table {
public:
  struct Storage;
  auto contains(TypeId type) const -> bool;
  auto get(TypeId type) const -> const Record &;
  auto inhabited(TypeId type) const -> bool;
  auto copy(TypeId type) const -> bool;
  auto describe(TypeId type) const -> std::string;
  auto records() const -> std::span<const Record>;
  auto id(std::uint32_t index) const -> TypeId;
  auto identity() const -> const void * { return Data.get(); }

private:
  friend class Builder;
  explicit Table(std::shared_ptr<const Storage> data) : Data(std::move(data)) {}
  std::shared_ptr<const Storage> Data;
};

class Builder {
public:
  Builder();
  Builder(const Builder &) = delete;
  auto operator=(const Builder &) -> Builder & = delete;
  auto declare(Kind kind, std::string name) -> TypeId;
  auto define(TypeId type, std::vector<Field> fields,
              std::vector<Variant> variants = {}) -> void;
  auto tuple(std::vector<TypeId> elements) -> TypeId;
  auto array(TypeId element, std::uint64_t length) -> TypeId;
  auto requestCopy(TypeId type) -> bool;
  auto get(TypeId type) const -> const Record &;
  auto complete(std::vector<Error> &errors) -> bool;
  auto inhabited(TypeId type) const -> bool;
  auto copy(TypeId type) const -> bool;
  auto finish(std::vector<Error> &errors) -> std::optional<Table>;

private:
  auto createRecord(Kind kind, std::string name) -> TypeId;
  std::shared_ptr<Table::Storage> Data;
  bool Frozen = false;
};

auto Name(Primitive primitive) -> std::string_view;
auto Lookup(std::string_view name) -> std::optional<TypeId>;
auto IsInteger(Primitive primitive) -> bool;
auto IsSigned(Primitive primitive) -> bool;
auto IsFloat(Primitive primitive) -> bool;
auto IsNumeric(Primitive primitive) -> bool;
inline auto IsInteger(TypeId type) -> bool {
  return IsInteger(type.primitive());
}
inline auto IsSigned(TypeId type) -> bool { return IsSigned(type.primitive()); }
inline auto IsFloat(TypeId type) -> bool { return IsFloat(type.primitive()); }
inline auto IsNumeric(TypeId type) -> bool {
  return IsNumeric(type.primitive());
}
} // namespace Chtholly::Types
