// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/Source/source.h>

namespace Chtholly::Lex {

enum class TokenKind {
  Fn,
  Return,
  Let,
  Var,
  If,
  Else,
  As,
  True,
  False,
  Struct,
  Enum,
  Alias,
  Const,
  Impl,
  For,
  Pub,
  Copy,
  Move,
  Match,
  While,
  Loop,
  Break,
  Continue,
  UnsupportedKeyword,
  Identifier,
  Integer,
  Float,
  LeftParen,
  RightParen,
  Arrow,
  LeftBrace,
  RightBrace,
  Semicolon,
  Colon,
  Comma,
  LeftBracket,
  RightBracket,
  Dot,
  Rest,
  Path,
  FatArrow,
  At,
  Assign,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Bang,
  Tilde,
  Amp,
  Pipe,
  Caret,
  ShiftLeft,
  ShiftRight,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Equal,
  NotEqual,
  And,
  Or,
  EndOfFile
};

struct Token {
  TokenKind Kind;
  Source::Span Text;
  unsigned Radix = 10;
  std::size_t SuffixLength = 0;
};

} // namespace Chtholly::Lex
