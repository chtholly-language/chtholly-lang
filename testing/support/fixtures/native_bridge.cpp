// SPDX-License-Identifier: MIT OR Apache-2.0
// This fixture is compiled separately from the tested language function. The
// generated header adapts its one signature to the fixed bit-string protocol.
#include <Chtholly/Runtime/abi.h>

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using Chtholly::Runtime::Bits;
#ifdef CHTHOLLY_BRIDGE_TEMPLATE
inline constexpr std::array<std::string_view, 0> InputTypes{};
inline constexpr std::array<unsigned, 0> InputBytes{};
inline constexpr std::string_view OutputType = "()";
inline constexpr unsigned OutputBytes = 0;
auto BridgeInvoke(std::span<const Bits>) -> Bits { return {}; }
#else
#include "bridge_config.h"
#endif

namespace {
auto Hex(char c) -> int {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}
auto Decode(std::string_view input, std::string_view type, unsigned bytes,
            Bits &result) -> bool {
  if (!input.starts_with(type) || input.size() != type.size() + 1 + bytes * 2 ||
      input[type.size()] != ':') {
    return false;
  }
  input.remove_prefix(type.size() + 1);
  result = {};
  for (unsigned i = 0; i < bytes; ++i) {
    const auto high = Hex(input[i * 2]);
    const auto low = Hex(input[i * 2 + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    auto &word = i < 8 ? result.Low : result.High;
    word |= static_cast<std::uint64_t>(high * 16 + low) << ((i % 8) * 8);
  }
  return type != "bool" || result.Low <= 1;
}
} // namespace
auto main(int argc, char **argv) -> int {
  if (argc != static_cast<int>(InputTypes.size()) + 1) {
    return 2;
  }
  std::vector<Bits> inputs(InputTypes.size());
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    if (!Decode(argv[i + 1], InputTypes[i], InputBytes[i], inputs[i])) {
      return 2;
    }
  }
  Chtholly::Runtime::chtholly_runtime_abi_v2();
  Chtholly::Runtime::chtholly_runtime_initialize();
  const auto result = BridgeInvoke(inputs);
  constexpr char Digits[] = "0123456789abcdef";
  std::string text(OutputType);
  text += ':';
  for (unsigned i = 0; i < OutputBytes; ++i) {
    const auto word = i < 8 ? result.Low : result.High;
    const auto byte = static_cast<unsigned>((word >> ((i % 8) * 8)) & 255);
    text += Digits[byte >> 4];
    text += Digits[byte & 15];
  }
  std::cout << text << '\n';
  return 0;
}
