// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/IR/ir.h>
#include <Chtholly/SemIR/program.h>
namespace Chtholly::Lower {
// Lower owns its output. It never republishes an AST or mutable SemIR view.
auto Lower(const SemIR::Program &program,
           Diagnostic::Diagnostics &diagnostics) -> std::optional<IR::Program>;
} // namespace Chtholly::Lower
