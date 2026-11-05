// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace Chtholly::Target {

enum class Type : std::uint8_t {
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
  Never,
  MathematicalInteger
};

struct Description {
  std::string_view Triple = "x86_64-pc-windows-msvc";
  unsigned PointerBits = 64;
  bool LittleEndian = true;

  constexpr auto valid() const -> bool {
    return Triple == "x86_64-pc-windows-msvc" && PointerBits == 64 &&
           LittleEndian;
  }

  constexpr auto bits(Type type) const -> unsigned {
    switch (type) {
    case Type::I8:
    case Type::U8:
      return 8;
    case Type::I16:
    case Type::U16:
      return 16;
    case Type::I32:
    case Type::U32:
    case Type::F32:
      return 32;
    case Type::I64:
    case Type::U64:
    case Type::F64:
      return 64;
    case Type::I128:
    case Type::U128:
      return 128;
    case Type::ISize:
    case Type::USize:
      return PointerBits;
    case Type::Bool:
      return 1;
    default:
      return 0;
    }
  }
  auto alignment(Type type) const -> unsigned;
};

inline constexpr Description WindowsX64{};

auto Name(Type type) -> std::string_view;
auto Lookup(std::string_view name) -> std::optional<Type>;
auto IsInteger(Type type) -> bool;
auto IsSigned(Type type) -> bool;
auto IsFloat(Type type) -> bool;
auto IsNumeric(Type type) -> bool;
auto IsInhabited(Type type) -> bool;
auto IsCopy(Type type) -> bool;

} // namespace Chtholly::Target
