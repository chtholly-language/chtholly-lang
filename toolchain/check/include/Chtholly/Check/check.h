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

// Success owns semantic records and constants, independently of the AST.
auto Check(const Source::Buffer &source, const Parse::Tree &tree,
           Diagnostic::Diagnostics &diagnostics,
           Numeric::Limits limits = {}) -> std::optional<SemIR::Program>;

} // namespace Chtholly::Check
