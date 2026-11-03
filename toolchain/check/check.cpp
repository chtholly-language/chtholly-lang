// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Check/check.h>
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/Parse/parse.h>
#include <Chtholly/Source/source.h>

#include <cstdint>
#include <limits>

namespace Chtholly::Check {

class Checker {
public:
  static auto Run(const Source::Buffer &source, const Parse::Tree &tree,
                  Diagnostic::Diagnostics &diagnostics)
      -> std::optional<SemIR::Program> {
    if (source.slice(tree.FunctionName) != "main") {
      diagnostics.sourceError(tree.FunctionName.Begin,
                              "Bootstrap requires the single entry function 'main'");
      return std::nullopt;
    }
    if (source.slice(tree.ReturnType) != "i32") {
      diagnostics.sourceError(tree.ReturnType.Begin, "main must return i32");
      return std::nullopt;
    }
    const auto literal = source.slice(tree.ReturnLiteral);
    if (literal.empty()) {
      diagnostics.sourceError(tree.ReturnLiteral.Begin, "expected a decimal integer");
      return std::nullopt;
    }
    std::int32_t value = 0;
    constexpr auto Max = std::numeric_limits<std::int32_t>::max();
    for (char c : literal) {
      if (c < '0' || c > '9') {
        diagnostics.sourceError(tree.ReturnLiteral.Begin,
                                "expected a non-negative decimal integer");
        return std::nullopt;
      }
      const auto digit = static_cast<std::int32_t>(c - '0');
      // Check before arithmetic, including arbitrarily many leading zeroes.
      if (value > (Max - digit) / 10) {
        diagnostics.sourceError(tree.ReturnLiteral.Begin,
                                "return literal is outside the i32 range 0..2147483647");
        return std::nullopt;
      }
      value = value * 10 + digit;
    }
    return SemIR::Program(value);
  }
};

auto Check(const Source::Buffer &source, const Parse::Tree &tree,
           Diagnostic::Diagnostics &diagnostics) -> std::optional<SemIR::Program> {
  return Checker::Run(source, tree, diagnostics);
}

} // namespace Chtholly::Check
