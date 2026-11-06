// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/SemIR/program.h>

#include <optional>

namespace Chtholly::Source {
class Buffer;
}
namespace Chtholly::Parse {
struct Tree;
}
namespace Chtholly::Diagnostic {
class Diagnostics;
}

namespace Chtholly::Check {
struct PatternLimits {
  std::size_t Candidates = 100000;
  unsigned Depth = 256;
};

// Success owns semantic records and constants, independently of the AST.
auto Check(const Source::Buffer &source, const Parse::Tree &tree,
           Diagnostic::Diagnostics &diagnostics, Numeric::Limits limits = {},
           PatternLimits patternLimits = {}) -> std::optional<SemIR::Program>;

} // namespace Chtholly::Check
