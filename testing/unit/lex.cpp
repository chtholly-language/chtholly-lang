// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Lex/lex.h>
#include <Chtholly/Testing/source.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

namespace Chtholly::Testing {
namespace {

TEST(lex, token_spans) {
  SourceFile file("fn main() -> i32 { return 42; }");
  const auto source = file.read();
  Diagnostic::Diagnostics diagnostics;
  const auto tokens = Lex::Lex(source, diagnostics);
  ASSERT_TRUE(tokens);
  const std::vector<Lex::TokenKind> expected = {
      Lex::TokenKind::Fn,         Lex::TokenKind::Identifier,
      Lex::TokenKind::LeftParen,  Lex::TokenKind::RightParen,
      Lex::TokenKind::Arrow,      Lex::TokenKind::Identifier,
      Lex::TokenKind::LeftBrace,  Lex::TokenKind::Return,
      Lex::TokenKind::Integer,    Lex::TokenKind::Semicolon,
      Lex::TokenKind::RightBrace, Lex::TokenKind::EndOfFile,
  };
  ASSERT_EQ(tokens->size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ((*tokens)[i].Kind, expected[i]);
    EXPECT_LE((*tokens)[i].Text.Begin + (*tokens)[i].Text.Length,
              source.text().size());
  }
  EXPECT_EQ(source.slice((*tokens)[8].Text), "42");
  EXPECT_EQ((*tokens)[8].Text.Begin, 26U);
}

TEST(lex, eof_span) {
  SourceFile file(" \t// end");
  const auto source = file.read();
  Diagnostic::Diagnostics diagnostics;
  const auto tokens = Lex::Lex(source, diagnostics);
  ASSERT_TRUE(tokens);
  ASSERT_EQ(tokens->size(), 1U);
  EXPECT_EQ(tokens->front().Kind, Lex::TokenKind::EndOfFile);
  EXPECT_EQ(tokens->front().Text.Begin, source.text().size());
  EXPECT_EQ(tokens->front().Text.Length, 0U);
}

TEST(lex, identifier_boundaries) {
  SourceFile file("fnx return_value _0 fn return");
  const auto source = file.read();
  Diagnostic::Diagnostics diagnostics;
  const auto tokens = Lex::Lex(source, diagnostics);
  ASSERT_TRUE(tokens);
  ASSERT_EQ(tokens->size(), 6U);
  EXPECT_EQ((*tokens)[0].Kind, Lex::TokenKind::Identifier);
  EXPECT_EQ((*tokens)[1].Kind, Lex::TokenKind::Identifier);
  EXPECT_EQ((*tokens)[2].Kind, Lex::TokenKind::Identifier);
  EXPECT_EQ((*tokens)[3].Kind, Lex::TokenKind::Fn);
  EXPECT_EQ((*tokens)[4].Kind, Lex::TokenKind::Return);
}

TEST(lex, bom_and_comments) {
  SourceFile file("\xEF\xBB\xBF"
                  "// \xE4\xB8\xAD\r\nfn");
  const auto source = file.read();
  Diagnostic::Diagnostics diagnostics;
  const auto tokens = Lex::Lex(source, diagnostics);
  ASSERT_TRUE(tokens);
  ASSERT_EQ(tokens->size(), 2U);
  EXPECT_EQ(source.locate(tokens->front().Text.Begin).Line, 2U);
}

TEST(lex, invalid_utf8) {
  const std::vector<std::string> invalid = {
      "\xFF", "\xC0\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE2\x82",
  };
  for (const auto &bytes : invalid) {
    SourceFile file("// " + bytes);
    const auto source = file.read();
    Diagnostic::Diagnostics diagnostics;
    EXPECT_FALSE(Lex::Lex(source, diagnostics));
    EXPECT_NE(Render(diagnostics, source).find("invalid UTF-8"),
              std::string::npos);
  }
}

TEST(lex, unsupported_characters) {
  for (const auto *text : {"\xC3\xA9", ".5", "1.", "/*", "`bad name`", "0b2",
                           "1__2", "1_", "1f16"}) {
    SourceFile file(text);
    const auto source = file.read();
    Diagnostic::Diagnostics diagnostics;
    EXPECT_FALSE(Lex::Lex(source, diagnostics));
  }
}

TEST(lex, core_tokens_and_nested_comments) {
  SourceFile file(
      "/* outer /* nested */ end */ `if` ! != 0xffu8 1f32 1.0e-2f64");
  const auto source = file.read();
  Diagnostic::Diagnostics diagnostics;
  const auto tokens = Lex::Lex(source, diagnostics);
  ASSERT_TRUE(tokens);
  ASSERT_EQ(tokens->size(), 7U);
  EXPECT_EQ((*tokens)[0].Kind, Lex::TokenKind::Identifier);
  EXPECT_EQ((*tokens)[1].Kind, Lex::TokenKind::Bang);
  EXPECT_EQ((*tokens)[2].Kind, Lex::TokenKind::NotEqual);
  EXPECT_EQ((*tokens)[3].Kind, Lex::TokenKind::Integer);
  EXPECT_EQ((*tokens)[3].Radix, 16U);
  EXPECT_EQ((*tokens)[3].SuffixLength, 2U);
  EXPECT_EQ((*tokens)[4].Kind, Lex::TokenKind::Float);
}

} // namespace
} // namespace Chtholly::Testing
