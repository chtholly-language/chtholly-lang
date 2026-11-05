// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Target/target.h>

#include <array>

namespace Chtholly::Target {
namespace {
constexpr std::array<std::string_view, 19> Names = {
    "<invalid>",
    "i8",
    "i16",
    "i32",
    "i64",
    "i128",
    "u8",
    "u16",
    "u32",
    "u64",
    "u128",
    "isize",
    "usize",
    "f32",
    "f64",
    "bool",
    "()",
    "!",
    "<mathematical integer>",
};
} // namespace

auto Name(Type type) -> std::string_view {
  const auto index = static_cast<unsigned>(type);
  return index < Names.size() ? Names[index] : Names[0];
}

auto Lookup(std::string_view name) -> std::optional<Type> {
  for (unsigned i = 1; i <= static_cast<unsigned>(Type::Never); ++i) {
    if (Names[i] == name) {
      return static_cast<Type>(i);
    }
  }
  return std::nullopt;
}

auto IsInteger(Type type) -> bool {
  return type >= Type::I8 && type <= Type::USize;
}
auto IsSigned(Type type) -> bool {
  return (type >= Type::I8 && type <= Type::I128) || type == Type::ISize;
}
auto IsFloat(Type type) -> bool {
  return type == Type::F32 || type == Type::F64;
}
auto IsNumeric(Type type) -> bool { return IsInteger(type) || IsFloat(type); }
auto IsInhabited(Type type) -> bool {
  return type >= Type::I8 && type <= Type::Unit;
}
auto IsCopy(Type type) -> bool {
  return IsNumeric(type) || type == Type::Bool || type == Type::Unit;
}

auto Description::alignment(Type type) const -> unsigned {
  if (type == Type::Unit) {
    return 1;
  }
  const auto width = bits(type);
  return width == 0 ? 0 : (width + 7) / 8;
}
} // namespace Chtholly::Target
