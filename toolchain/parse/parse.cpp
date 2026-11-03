// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Parse/parse.h>
#include <Chtholly/Diagnostic/diagnostic.h>

#include <cstddef>
#include <string>

namespace Chtholly::Parse {

auto Parse(std::span<const Lex::Token> tokens,
           Diagnostic::Diagnostics &diagnostics) -> std::optional<Tree> {
  using Lex::TokenKind;
  std::size_t cursor = 0;
  const auto expect = [&](TokenKind kind, const char *description,
                          Source::Span *span = nullptr) -> bool {
    if (cursor == tokens.size() || tokens[cursor].Kind != kind) {
      const auto offset = cursor < tokens.size() ? tokens[cursor].Text.Begin
                          : tokens.empty() ? 0 : tokens.back().Text.Begin;
      diagnostics.sourceError(offset, std::string("expected ") + description);
      return false;
    }
    if (span) {
      *span = tokens[cursor].Text;
    }
    ++cursor;
    return true;
  };
  Tree tree{};
  if (!expect(TokenKind::Fn, "'fn'") ||
      !expect(TokenKind::Identifier, "a function name", &tree.FunctionName) ||
      !expect(TokenKind::LeftParen, "'('") ||
      !expect(TokenKind::RightParen, "')' (Bootstrap does not support parameters)") ||
      !expect(TokenKind::Arrow, "'->'") ||
      !expect(TokenKind::Identifier, "a return type", &tree.ReturnType) ||
      !expect(TokenKind::LeftBrace, "'{'") ||
      !expect(TokenKind::Return, "'return'") ||
      !expect(TokenKind::DecimalInteger, "a non-negative decimal integer", &tree.ReturnLiteral) ||
      !expect(TokenKind::Semicolon, "';' after the return literal") ||
      !expect(TokenKind::RightBrace, "'}'") ||
      !expect(TokenKind::EndOfFile, "end of input (only one function is supported)")) {
    return std::nullopt;
  }
  if (cursor != tokens.size()) {
    diagnostics.sourceError(tokens[cursor].Text.Begin, "unexpected token after end of input");
    return std::nullopt;
  }
  return tree;
}

} // namespace Chtholly::Parse
