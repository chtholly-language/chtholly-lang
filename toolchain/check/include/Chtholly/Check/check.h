// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/SemIR/program.h>

#include <optional>

namespace Chtholly::Source { class Buffer; }
namespace Chtholly::Parse { struct Tree; }
namespace Chtholly::Diagnostic { class Diagnostics; }

namespace Chtholly::Check {

// tree is the successful Parse result for source. No checked program is
// produced unless the Bootstrap entry and decimal range requirements hold.
auto Check(const Source::Buffer &source, const Parse::Tree &tree,
           Diagnostic::Diagnostics &diagnostics) -> std::optional<SemIR::Program>;

} // namespace Chtholly::Check
