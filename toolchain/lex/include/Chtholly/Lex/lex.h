// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/Lex/token.h>

#include <optional>
#include <vector>

namespace Chtholly::Diagnostic {
class Diagnostics;
}

namespace Chtholly::Lex {

// Returns a complete token stream ending in EOF, or no stream on any error.
auto Lex(const Source::Buffer &source, Diagnostic::Diagnostics &diagnostics)
    -> std::optional<std::vector<Token>>;

} // namespace Chtholly::Lex
