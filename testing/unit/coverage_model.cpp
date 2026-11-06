// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Testing/frontend.h>
#include <array>
#include <gtest/gtest.h>

namespace Chtholly::Testing {
TEST(coverage, independent_finite_domain) {
  // Each bit denotes a real pair of booleans. Expected coverage comes from
  // enumerating four source values, independently of production coverage.
  for (unsigned mask = 0; mask < 16; ++mask) {
    std::string arms;
    for (unsigned value = 0; value < 4; ++value) {
      if ((mask & (1U << value)) == 0) {
        continue;
      }
      arms += std::string("(") + ((value & 1) != 0 ? "true" : "false") + ", " +
              ((value & 2) != 0 ? "true" : "false") + ") => (), ";
    }
    const auto source = "fn probe(_ pair: (bool, bool)) { match (pair) { " +
                        arms + " }; } fn main() -> i32 { return 0; }";
    EXPECT_EQ(Accepts(source), mask == 15) << source;
    const auto withFallback =
        "fn probe(_ pair: (bool, bool)) { match (pair) { " + arms +
        " _ => (), }; } fn main() -> i32 { return 0; }";
    EXPECT_TRUE(Accepts(withFallback));
  }
}
TEST(coverage, wildcard_products_and_guard_exclusion) {
  constexpr std::array<std::string_view, 3> Atoms = {"false", "true", "_"};
  for (unsigned left = 0; left < 9; ++left) {
    for (unsigned right = 0; right < 9; ++right) {
      bool expected = true;
      for (unsigned value = 0; value < 4; ++value) {
        const auto matches = [&](unsigned pattern) {
          const auto first = pattern % 3;
          const auto second = pattern / 3;
          return (first == 2 || first == (value & 1)) &&
                 (second == 2 || second == ((value >> 1) & 1));
        };
        expected &= matches(left) || matches(right);
      }
      const auto spelling = [&](unsigned pattern) {
        return "(" + std::string(Atoms[pattern % 3]) + ", " +
               std::string(Atoms[pattern / 3]) + ")";
      };
      const auto source = "fn probe(_ pair: (bool, bool)) { match (pair) { " +
                          spelling(left) + " => (), " + spelling(right) +
                          " => (), }; } fn main() -> i32 { return 0; }";
      EXPECT_EQ(Accepts(source), expected) << source;
    }
  }
  EXPECT_FALSE(
      Accepts("fn probe(_ b: bool) { match (b) { _ if (true) => (), }; } "
              "fn main() -> i32 { return 0; }"));
  std::string integerArms;
  for (unsigned value = 0; value < 256; ++value) {
    integerArms += std::to_string(value) + " => (), ";
  }
  EXPECT_FALSE(Accepts("fn probe(_ n: u8) { match (n) { " + integerArms +
                       " }; } fn main() -> i32 { return 0; }"));
  EXPECT_TRUE(Accepts("fn probe(_ n: u8) { match (n) { " + integerArms +
                      " _ => (), }; } fn main() -> i32 { return 0; }"));
}
} // namespace Chtholly::Testing
