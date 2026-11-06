// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Target/target.h>

#include <array>
#include <stdexcept>

namespace Chtholly::Target {
namespace {
constexpr std::array<std::string_view, 18> Names = {
    "<invalid>", "i8",   "i16",   "i32",   "i64", "i128", "u8",   "u16", "u32",
    "u64",       "u128", "isize", "usize", "f32", "f64",  "bool", "()",  "!",
};
} // namespace

auto Name(Scalar type) -> std::string_view {
  const auto index = static_cast<unsigned>(type);
  return index < Names.size() ? Names[index] : Names[0];
}

auto Lookup(std::string_view name) -> std::optional<Scalar> {
  for (unsigned i = 1; i <= static_cast<unsigned>(Scalar::Never); ++i) {
    if (Names[i] == name) {
      return static_cast<Scalar>(i);
    }
  }
  return std::nullopt;
}

auto IsInteger(Scalar type) -> bool {
  return type >= Scalar::I8 && type <= Scalar::USize;
}
auto IsSigned(Scalar type) -> bool {
  return (type >= Scalar::I8 && type <= Scalar::I128) || type == Scalar::ISize;
}
auto IsFloat(Scalar type) -> bool {
  return type == Scalar::F32 || type == Scalar::F64;
}
auto IsNumeric(Scalar type) -> bool { return IsInteger(type) || IsFloat(type); }
auto IsInhabited(Scalar type) -> bool {
  return type >= Scalar::I8 && type <= Scalar::Unit;
}
auto IsCopy(Scalar type) -> bool {
  return IsNumeric(type) || type == Scalar::Bool || type == Scalar::Unit;
}

auto Description::alignment(Scalar type) const -> unsigned {
  if (type == Scalar::Unit) {
    return 1;
  }
  const auto width = bits(type);
  return width == 0 ? 0 : (width + 7) / 8;
}
auto ScalarOf(Types::TypeId type) -> Scalar {
  using P = Types::Primitive;
  switch (type.primitive()) {
  case P::I8:
    return Scalar::I8;
  case P::I16:
    return Scalar::I16;
  case P::I32:
    return Scalar::I32;
  case P::I64:
    return Scalar::I64;
  case P::I128:
    return Scalar::I128;
  case P::U8:
    return Scalar::U8;
  case P::U16:
    return Scalar::U16;
  case P::U32:
    return Scalar::U32;
  case P::U64:
    return Scalar::U64;
  case P::U128:
    return Scalar::U128;
  case P::ISize:
    return Scalar::ISize;
  case P::USize:
    return Scalar::USize;
  case P::F32:
    return Scalar::F32;
  case P::F64:
    return Scalar::F64;
  case P::Bool:
    return Scalar::Bool;
  case P::Unit:
    return Scalar::Unit;
  case P::Never:
    return Scalar::Never;
  default:
    return Scalar::Invalid;
  }
}
auto SourceType(Scalar scalar) -> Types::TypeId {
  // This mapping is not a wire cast. Mathematical integers have no source ID.
  const auto primitive = Types::Lookup(Name(scalar));
  return primitive ? Types::TypeId(*primitive) : Types::TypeId::Invalid;
}
auto Description::bits(Types::TypeId type) const -> unsigned {
  return bits(ScalarOf(type));
}
} // namespace Chtholly::Target
