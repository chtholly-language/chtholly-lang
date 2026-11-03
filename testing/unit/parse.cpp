// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Lex/lex.h>
#include <Chtholly/Parse/parse.h>
#include <Chtholly/Testing/source.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace Chtholly::Testing {
namespace {

TEST(parse, minimum_tree) {
  SourceFile file("fn main() -> i32 { return 0042; }");
  const auto source = file.read();
  Diagnostic::Diagnostics diagnostics;
  const auto tokens = Lex::Lex(source, diagnostics);
  ASSERT_TRUE(tokens);
  const auto tree = Parse::Parse(*tokens, diagnostics);
  ASSERT_TRUE(tree);
  EXPECT_EQ(source.slice(tree->FunctionName), "main");
  EXPECT_EQ(source.slice(tree->ReturnType), "i32");
  EXPECT_EQ(source.slice(tree->ReturnLiteral), "0042");
}

TEST(parse, structural_failures) {
  const std::vector<std::string> inputs = {
      "",
      "fn main() -> i32 {}",
      "fn main(x) -> i32 { return 0; }",
      "fn main() -> i32 { return 0 }",
      "fn main() -> i32 { return 0; } extra",
      "fn main() -> i32 { return 0; } fn other() -> i32 { return 0; }",
      "fn main() -> i32 { return 1u32; }",
      "fn main() -> i32 { return 0x10; }"};
  for (const auto &text : inputs) {
    SourceFile file(text);
    const auto source = file.read();
    Diagnostic::Diagnostics diagnostics;
    const auto tokens = Lex::Lex(source, diagnostics);
    ASSERT_TRUE(tokens) << "the negative must reach Parse";
    EXPECT_FALSE(Parse::Parse(*tokens, diagnostics));
  }
}

} // namespace
} // namespace Chtholly::Testing
