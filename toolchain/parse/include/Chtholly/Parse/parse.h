// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/Lex/token.h>

#include <optional>
#include <span>

namespace Chtholly::Diagnostic {
class Diagnostics;
}

namespace Chtholly::Parse {

struct Tree {
  Source::Span FunctionName;
  Source::Span ReturnType;
  Source::Span ReturnLiteral;
};

// Tokens must come from Lex on one still-live Source::Buffer. Returned spans
// refer to that same buffer; parsing never resolves names or numeric values.
auto Parse(std::span<const Lex::Token> tokens,
           Diagnostic::Diagnostics &diagnostics) -> std::optional<Tree>;

} // namespace Chtholly::Parse
