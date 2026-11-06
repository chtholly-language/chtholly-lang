// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once

#include <Chtholly/Types/types.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace Chtholly::Target {

enum class Scalar : std::uint8_t {
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

struct Description {
  std::string_view Triple = "x86_64-pc-windows-msvc";
  unsigned PointerBits = 64;
  bool LittleEndian = true;

  constexpr auto valid() const -> bool {
    return Triple == "x86_64-pc-windows-msvc" && PointerBits == 64 &&
           LittleEndian;
  }

  constexpr auto bits(Scalar type) const -> unsigned {
    switch (type) {
    case Scalar::I8:
    case Scalar::U8:
      return 8;
    case Scalar::I16:
    case Scalar::U16:
      return 16;
    case Scalar::I32:
    case Scalar::U32:
    case Scalar::F32:
      return 32;
    case Scalar::I64:
    case Scalar::U64:
    case Scalar::F64:
      return 64;
    case Scalar::I128:
    case Scalar::U128:
      return 128;
    case Scalar::ISize:
    case Scalar::USize:
      return PointerBits;
    case Scalar::Bool:
      return 1;
    default:
      return 0;
    }
  }
  auto alignment(Scalar type) const -> unsigned;
  auto bits(Types::TypeId type) const -> unsigned;
};

inline constexpr Description WindowsX64{};

auto Name(Scalar type) -> std::string_view;
auto Lookup(std::string_view name) -> std::optional<Scalar>;
auto IsInteger(Scalar type) -> bool;
auto IsSigned(Scalar type) -> bool;
auto IsFloat(Scalar type) -> bool;
auto IsNumeric(Scalar type) -> bool;
auto IsInhabited(Scalar type) -> bool;
auto IsCopy(Scalar type) -> bool;

// Scalar categories and program-owned type IDs are deliberately separate.
auto ScalarOf(Types::TypeId type) -> Scalar;
auto SourceType(Scalar scalar) -> Types::TypeId;
inline auto IsInteger(Types::TypeId type) -> bool {
  return Types::IsInteger(type);
}
inline auto IsSigned(Types::TypeId type) -> bool {
  return Types::IsSigned(type);
}
inline auto IsFloat(Types::TypeId type) -> bool { return Types::IsFloat(type); }
inline auto IsNumeric(Types::TypeId type) -> bool {
  return Types::IsNumeric(type);
}

} // namespace Chtholly::Target
