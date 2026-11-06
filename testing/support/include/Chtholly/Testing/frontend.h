// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/Check/check.h>
#include <Chtholly/Flow/flow.h>
#include <Chtholly/Lex/lex.h>
#include <Chtholly/Lower/lower.h>
#include <Chtholly/Parse/parse.h>
#include <Chtholly/Testing/source.h>

namespace Chtholly::Testing {
// Uses real temporary files and the public pipeline. No in-memory source API.
inline auto Checked(std::string_view text, Diagnostic::Diagnostics &diagnostics,
                    Numeric::Limits limits = {},
                    Check::PatternLimits patternLimits = {})
    -> std::optional<SemIR::Program> {
  SourceFile file{std::string(text)};
  const auto source = file.read();
  const auto tokens = Lex::Lex(source, diagnostics);
  if (!tokens) {
    return std::nullopt;
  }
  const auto tree = Parse::Parse(*tokens, diagnostics);
  if (!tree) {
    return std::nullopt;
  }
  return Check::Check(source, *tree, diagnostics, limits, patternLimits);
}
inline auto Accepts(std::string_view text) -> bool {
  Diagnostic::Diagnostics diagnostics;
  return Checked(text, diagnostics).has_value();
}
inline auto Executable(std::string_view text,
                       Diagnostic::Diagnostics &diagnostics,
                       Flow::Limits limits = {})
    -> std::optional<Flow::InitializationCheckedView> {
  const auto checked = Checked(text, diagnostics);
  if (!checked) {
    return std::nullopt;
  }
  const auto ir = Lower::Lower(*checked, diagnostics);
  return ir ? Flow::Analyze(*ir, diagnostics, limits) : std::nullopt;
}
} // namespace Chtholly::Testing
