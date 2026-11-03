// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/Source/source.h>

namespace Chtholly::Lex {

enum class TokenKind {
  Fn, Return, Identifier, DecimalInteger, LeftParen, RightParen, Arrow,
  LeftBrace, RightBrace, Semicolon, EndOfFile
};

struct Token {
  TokenKind Kind;
  Source::Span Text;
};

} // namespace Chtholly::Lex
