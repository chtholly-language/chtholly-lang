// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Check/check.h>
#include <Chtholly/Lex/lex.h>
#include <Chtholly/Parse/parse.h>
#include <Chtholly/Testing/source.h>

#include <gtest/gtest.h>

#include <string>

namespace Chtholly::Testing {
namespace {

TEST(check, integer_boundaries) {
  for (const auto *digits : {"0", "1", "42", "2147483647"}) {
    SourceFile file(std::string("fn main() -> i32 { return ") + digits + "; }");
    const auto source = file.read();
    Diagnostic::Diagnostics diagnostics;
    const auto tokens = Lex::Lex(source, diagnostics);
    ASSERT_TRUE(tokens);
    const auto tree = Parse::Parse(*tokens, diagnostics);
    ASSERT_TRUE(tree);
    const auto program = Check::Check(source, *tree, diagnostics);
    ASSERT_TRUE(program);
    EXPECT_EQ(program->returnValue(), std::stoll(digits));
  }
}

TEST(check, rejects_entry_type_and_overflow) {
  for (const auto *text :
       {"fn other() -> i32 { return 0; }", "fn main() -> u32 { return 0; }",
        "fn main() -> i32 { return 2147483648; }"}) {
    SourceFile file(text);
    const auto source = file.read();
    Diagnostic::Diagnostics diagnostics;
    const auto tokens = Lex::Lex(source, diagnostics);
    ASSERT_TRUE(tokens);
    const auto tree = Parse::Parse(*tokens, diagnostics);
    ASSERT_TRUE(tree) << "the negative must reach Check";
    EXPECT_FALSE(Check::Check(source, *tree, diagnostics));
  }
}

TEST(check, long_decimal) {
  for (bool overflow : {false, true}) {
    SourceFile file("fn main() -> i32 { return " +
                    std::string(4096, overflow ? '9' : '0') + "42; }");
    const auto source = file.read();
    Diagnostic::Diagnostics diagnostics;
    const auto tokens = Lex::Lex(source, diagnostics);
    ASSERT_TRUE(tokens);
    const auto tree = Parse::Parse(*tokens, diagnostics);
    ASSERT_TRUE(tree);
    const auto program = Check::Check(source, *tree, diagnostics);
    EXPECT_EQ(program.has_value(), !overflow);
    if (program) {
      EXPECT_EQ(program->returnValue(), 42);
    }
  }
}

} // namespace
} // namespace Chtholly::Testing
