// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/Lex/lex.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace Chtholly::Lex {
namespace {

auto IsDigit(unsigned char c) -> bool { return c >= '0' && c <= '9'; }
auto IsIdentifierStart(unsigned char c) -> bool {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

auto Digit(unsigned char c) -> unsigned {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return 16;
}

auto Keyword(std::string_view word) -> TokenKind {
  constexpr std::pair<std::string_view, TokenKind> Supported[] = {
      {"fn", TokenKind::Fn},      {"return", TokenKind::Return},
      {"let", TokenKind::Let},    {"var", TokenKind::Var},
      {"if", TokenKind::If},      {"else", TokenKind::Else},
      {"as", TokenKind::As},      {"true", TokenKind::True},
      {"false", TokenKind::False}};
  for (const auto &[spelling, kind] : Supported) {
    if (word == spelling) {
      return kind;
    }
  }
  constexpr std::array Unsupported = {
      "struct", "enum",     "module", "pub",   "in",     "opaque", "mut",
      "null",   "const",    "static", "while", "loop",   "for",    "match",
      "break",  "continue", "copy",   "move",  "unsafe", "extern",
  };
  for (const auto *keyword : Unsupported) {
    if (word == keyword) {
      return TokenKind::UnsupportedKeyword;
    }
  }
  return TokenKind::Identifier;
}

// Validate even bytes in comments. Reject overlong forms, surrogates,
// truncation and scalars beyond U+10FFFF before interpreting token boundaries.
auto ValidateUTF8(std::string_view text,
                  Diagnostic::Diagnostics &diagnostics) -> bool {
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i]);
    if (first < 0x80) {
      ++i;
      continue;
    }
    const std::size_t length = first >= 0xC2 && first <= 0xDF   ? 2
                               : first >= 0xE0 && first <= 0xEF ? 3
                               : first >= 0xF0 && first <= 0xF4 ? 4
                                                                : 0;
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
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
        c == '\v') {
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
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      const auto begin = i;
      std::size_t nesting = 1;
      i += 2;
      while (i < text.size() && nesting != 0) {
        if (i + 1 < text.size() && text.substr(i, 2) == "/*") {
          ++nesting;
          i += 2;
        } else if (i + 1 < text.size() && text.substr(i, 2) == "*/") {
          --nesting;
          i += 2;
        } else {
          ++i;
        }
      }
      if (nesting != 0) {
        diagnostics.sourceError(begin, "unterminated block comment");
        return std::nullopt;
      }
      continue;
    }
    const auto begin = i;
    if (c == '`') {
      ++i;
      if (i == text.size() ||
          !IsIdentifierStart(static_cast<unsigned char>(text[i]))) {
        diagnostics.sourceError(begin,
                                "escaped name must be an ASCII identifier");
        return std::nullopt;
      }
      while (i < text.size() &&
             (IsIdentifierStart(static_cast<unsigned char>(text[i])) ||
              IsDigit(static_cast<unsigned char>(text[i])))) {
        ++i;
      }
      if (i == text.size() || text[i] != '`') {
        diagnostics.sourceError(begin,
                                "unterminated or invalid escaped identifier");
        return std::nullopt;
      }
      ++i;
      tokens.push_back({TokenKind::Identifier, {begin, i - begin}});
      continue;
    }
    if (IsIdentifierStart(c)) {
      do {
        ++i;
      } while (i < text.size() &&
               (IsIdentifierStart(static_cast<unsigned char>(text[i])) ||
                IsDigit(static_cast<unsigned char>(text[i]))));
      const auto word = text.substr(begin, i - begin);
      const auto kind = Keyword(word);
      tokens.push_back({kind, {begin, i - begin}});
      continue;
    }
    if (IsDigit(c)) {
      unsigned radix = 10;
      if (text[i] == '0' && i + 1 < text.size()) {
        switch (text[i + 1]) {
        case 'b':
          radix = 2;
          break;
        case 'o':
          radix = 8;
          break;
        case 'x':
          radix = 16;
          break;
        default:
          break;
        }
        if (radix != 10) {
          i += 2;
        }
      }
      const auto scanDigits = [&](unsigned base) -> bool {
        bool any = false;
        bool separator = false;
        while (i < text.size()) {
          if (Digit(static_cast<unsigned char>(text[i])) < base) {
            any = true;
            separator = false;
            ++i;
          } else if (text[i] == '_') {
            if (!any || separator) {
              return false;
            }
            separator = true;
            ++i;
          } else {
            break;
          }
        }
        return any && !separator;
      };
      bool valid = scanDigits(radix);
      bool floating = false;
      if (radix == 10 && i < text.size() && text[i] == '.' &&
          !(i + 1 < text.size() && text[i + 1] == '.')) {
        floating = true;
        ++i;
        valid = scanDigits(10) && valid;
      }
      if (radix == 10 && i < text.size() &&
          (text[i] == 'e' || text[i] == 'E')) {
        floating = true;
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
          ++i;
        }
        valid = scanDigits(10) && valid;
      }
      const auto suffix = i;
      while (i < text.size() &&
             (IsIdentifierStart(static_cast<unsigned char>(text[i])) ||
              IsDigit(static_cast<unsigned char>(text[i])))) {
        ++i;
      }
      const auto name = text.substr(suffix, i - suffix);
      constexpr std::array Suffixes = {"i8",    "i16",   "i32", "i64", "i128",
                                       "u8",    "u16",   "u32", "u64", "u128",
                                       "isize", "usize", "f32", "f64"};
      bool known = name.empty();
      for (const auto *candidate : Suffixes) {
        known |= name == candidate;
      }
      if (name == "f32" || name == "f64") {
        valid &= radix == 10;
        floating = true;
      } else if (floating && !name.empty()) {
        valid = false;
      }
      if (!valid || !known) {
        diagnostics.sourceError(
            begin, "invalid numeric literal, separator, or suffix");
        return std::nullopt;
      }
      tokens.push_back({floating ? TokenKind::Float : TokenKind::Integer,
                        {begin, i - begin},
                        radix,
                        i - suffix});
      continue;
    }
    if (i + 1 < text.size()) {
      const auto pair = text.substr(i, 2);
      const std::array Pairs = {std::pair{"->", TokenKind::Arrow},
                                std::pair{"<<", TokenKind::ShiftLeft},
                                std::pair{">>", TokenKind::ShiftRight},
                                std::pair{"<=", TokenKind::LessEqual},
                                std::pair{">=", TokenKind::GreaterEqual},
                                std::pair{"==", TokenKind::Equal},
                                std::pair{"!=", TokenKind::NotEqual},
                                std::pair{"&&", TokenKind::And},
                                std::pair{"||", TokenKind::Or}};
      bool found = false;
      for (const auto &[spelling, token] : Pairs) {
        if (pair == spelling) {
          tokens.push_back({token, {begin, 2}});
          i += 2;
          found = true;
          break;
        }
      }
      if (found) {
        continue;
      }
    }
    TokenKind kind;
    switch (c) {
    case '(':
      kind = TokenKind::LeftParen;
      break;
    case ')':
      kind = TokenKind::RightParen;
      break;
    case '{':
      kind = TokenKind::LeftBrace;
      break;
    case '}':
      kind = TokenKind::RightBrace;
      break;
    case ';':
      kind = TokenKind::Semicolon;
      break;
    case ':':
      kind = TokenKind::Colon;
      break;
    case ',':
      kind = TokenKind::Comma;
      break;
    case '=':
      kind = TokenKind::Assign;
      break;
    case '+':
      kind = TokenKind::Plus;
      break;
    case '-':
      kind = TokenKind::Minus;
      break;
    case '*':
      kind = TokenKind::Star;
      break;
    case '/':
      kind = TokenKind::Slash;
      break;
    case '%':
      kind = TokenKind::Percent;
      break;
    case '!':
      kind = TokenKind::Bang;
      break;
    case '~':
      kind = TokenKind::Tilde;
      break;
    case '&':
      kind = TokenKind::Amp;
      break;
    case '|':
      kind = TokenKind::Pipe;
      break;
    case '^':
      kind = TokenKind::Caret;
      break;
    case '<':
      kind = TokenKind::Less;
      break;
    case '>':
      kind = TokenKind::Greater;
      break;
    default:
      diagnostics.sourceError(begin, "unsupported character (byte " +
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
