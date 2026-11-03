// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Lex/lex.h>
#include <Chtholly/Diagnostic/diagnostic.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace Chtholly::Lex {
namespace {

auto IsDigit(unsigned char c) -> bool { return c >= '0' && c <= '9'; }
auto IsIdentifierStart(unsigned char c) -> bool {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

// Validate even bytes in comments. Reject overlong forms, surrogates,
// truncation and scalars beyond U+10FFFF before interpreting token boundaries.
auto ValidateUTF8(std::string_view text, Diagnostic::Diagnostics &diagnostics)
    -> bool {
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i]);
    if (first < 0x80) {
      ++i;
      continue;
    }
    const std::size_t length = first >= 0xC2 && first <= 0xDF ? 2
                             : first >= 0xE0 && first <= 0xEF ? 3
                             : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
    bool valid = length != 0 && length <= text.size() - i;
    if (valid) {
      for (std::size_t j = 1; j < length; ++j) {
        const auto byte = static_cast<unsigned char>(text[i + j]);
        valid &= byte >= 0x80 && byte <= 0xBF;
      }
      const auto second = static_cast<unsigned char>(text[i + 1]);
      valid &= !(first == 0xE0 && second < 0xA0);
      valid &= !(first == 0xED && second >= 0xA0);
      valid &= !(first == 0xF0 && second < 0x90);
      valid &= !(first == 0xF4 && second > 0x8F);
    }
    if (!valid) {
      diagnostics.sourceError(i, "invalid UTF-8 encoding");
      return false;
    }
    i += length;
  }
  return true;
}

} // namespace

auto Lex(const Source::Buffer &source, Diagnostic::Diagnostics &diagnostics)
    -> std::optional<std::vector<Token>> {
  const auto text = source.text();
  if (!ValidateUTF8(text, diagnostics)) {
    return std::nullopt;
  }
  std::vector<Token> tokens;
  std::size_t i = text.starts_with("\xEF\xBB\xBF") ? 3 : 0;
  while (i < text.size()) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      i += 2;
      while (i < text.size() && text[i] != '\r' && text[i] != '\n') {
        ++i;
      }
      continue;
    }
    const auto begin = i;
    if (IsIdentifierStart(c)) {
      do {
        ++i;
      } while (i < text.size() &&
               (IsIdentifierStart(static_cast<unsigned char>(text[i])) ||
                IsDigit(static_cast<unsigned char>(text[i]))));
      const auto word = text.substr(begin, i - begin);
      const auto kind = word == "fn" ? TokenKind::Fn
                      : word == "return" ? TokenKind::Return
                                         : TokenKind::Identifier;
      tokens.push_back({kind, {begin, i - begin}});
      continue;
    }
    if (IsDigit(c)) {
      do {
        ++i;
      } while (i < text.size() && IsDigit(static_cast<unsigned char>(text[i])));
      tokens.push_back({TokenKind::DecimalInteger, {begin, i - begin}});
      continue;
    }
    TokenKind kind;
    switch (c) {
    case '(': kind = TokenKind::LeftParen; break;
    case ')': kind = TokenKind::RightParen; break;
    case '{': kind = TokenKind::LeftBrace; break;
    case '}': kind = TokenKind::RightBrace; break;
    case ';': kind = TokenKind::Semicolon; break;
    case '-':
      if (i + 1 < text.size() && text[i + 1] == '>') {
        ++i;
        kind = TokenKind::Arrow;
        break;
      }
      [[fallthrough]];
    default:
      diagnostics.sourceError(begin, "unsupported character in Bootstrap input (byte " +
                                         std::to_string(c) + ")");
      return std::nullopt;
    }
    ++i;
    tokens.push_back({kind, {begin, i - begin}});
  }
  tokens.push_back({TokenKind::EndOfFile, {text.size(), 0}});
  return tokens;
}

} // namespace Chtholly::Lex
