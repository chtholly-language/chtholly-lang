// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Lex/lex.h>
#include <Chtholly/Parse/parse.h>
#include <Chtholly/Testing/source.h>
#include <gtest/gtest.h>

namespace Chtholly::Testing {
TEST(parse, minimum_tree) {
  SourceFile file("fn main() -> i32 { return 0042; }");
  const auto source = file.read();
  Diagnostic::Diagnostics diagnostics;
  const auto tokens = Lex::Lex(source, diagnostics);
  ASSERT_TRUE(tokens);
  const auto tree = Parse::Parse(*tokens, diagnostics);
  ASSERT_TRUE(tree);
  ASSERT_EQ(tree->Functions.size(), 1U);
  EXPECT_EQ(source.slice(tree->Functions[0].Name), "main");
  EXPECT_EQ(source.slice(tree->Functions[0].ReturnType.Text), "i32");
  const auto &body = tree->Nodes[tree->Functions[0].Body];
  ASSERT_EQ(body.Statements.size(), 1U);
  const auto &statement = tree->Nodes[body.Statements[0]];
  EXPECT_EQ(statement.Form, Parse::Kind::Return);
  EXPECT_EQ(source.slice(tree->Nodes[statement.Left].Text), "0042");
}
TEST(parse, structural_failures) {
  for (const auto *text : {
           "fn main(x) -> i32 {}",
           "fn main() -> i32 { return 0 }",
           "fn main() -> i32 {} extra",
           "fn main() { let x; }",
           "fn main() { 1 < 2 < 3; }",
           "fn main() { 1 == 2 != 3; }",
           "fn main() { var x = 0; x += 1; }",
           "fn main() { let (var ..,) = (42,); }",
           "fn main() { let (var .., value) = (0, 42); }",
       }) {
    SourceFile file(text);
    const auto source = file.read();
    Diagnostic::Diagnostics diagnostics;
    const auto tokens = Lex::Lex(source, diagnostics);
    ASSERT_TRUE(tokens);
    EXPECT_FALSE(Parse::Parse(*tokens, diagnostics)) << text;
  }
}
TEST(parse, invalid_declaration_points_to_token) {
  struct Case {
    const char *Text;
    const char *Location;
  };
  for (const auto &item : {
           Case{"fn main() -> i32 { return 0; } extra\n", ":1:32: error:"},
           Case{"pub extra name\n", ":1:5: error:"},
       }) {
    SourceFile file(item.Text);
    const auto source = file.read();
    Diagnostic::Diagnostics diagnostics;
    const auto tokens = Lex::Lex(source, diagnostics);
    ASSERT_TRUE(tokens);
    EXPECT_FALSE(Parse::Parse(*tokens, diagnostics));
    const auto rendered = Render(diagnostics, source);
    EXPECT_NE(rendered.find(item.Location), std::string::npos) << rendered;
    EXPECT_NE(rendered.find("expected a supported top-level declaration"),
              std::string::npos)
        << rendered;
    EXPECT_EQ(rendered.find(":2:1:"), std::string::npos) << rendered;
  }
}
TEST(parse, functions_precedence_and_never) {
  SourceFile file("fn stop() -> ! { abort(); } "
                  "fn main() -> i32 { let x = 1 + 2 * 3 as i32; return x; }");
  const auto source = file.read();
  Diagnostic::Diagnostics diagnostics;
  const auto tokens = Lex::Lex(source, diagnostics);
  ASSERT_TRUE(tokens);
  const auto tree = Parse::Parse(*tokens, diagnostics);
  ASSERT_TRUE(tree);
  EXPECT_EQ(tree->Functions.size(), 2U);
  EXPECT_EQ(source.slice(tree->Functions[0].ReturnType.Text), "!");
  const auto &body = tree->Nodes[tree->Functions[1].Body];
  const auto &binding = tree->Nodes[body.Statements[0]];
  const auto &sum = tree->Nodes[binding.Left];
  EXPECT_EQ(sum.Operator, Lex::TokenKind::Plus);
  const auto &product = tree->Nodes[sum.Right];
  EXPECT_EQ(product.Operator, Lex::TokenKind::Star);
  EXPECT_EQ(tree->Nodes[product.Right].Form, Parse::Kind::Convert);
}
} // namespace Chtholly::Testing
