// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Check/check.h>
#include <Chtholly/Lex/lex.h>
#include <Chtholly/Parse/parse.h>
#include <Chtholly/Testing/source.h>
#include <gtest/gtest.h>

namespace Chtholly::Testing {
TEST(frontend, checked_pipeline) {
  SourceFile file("// actual frontend\nfn main() -> i32 { return 42; }");
  const auto source = file.read();
  Diagnostic::Diagnostics diagnostics;
  const auto tokens = Lex::Lex(source, diagnostics);
  ASSERT_TRUE(tokens);
  const auto tree = Parse::Parse(*tokens, diagnostics);
  ASSERT_TRUE(tree);
  const auto program = Check::Check(source, *tree, diagnostics);
  ASSERT_TRUE(program);
  EXPECT_EQ(program->returnValue(), 42);
  EXPECT_TRUE(Render(diagnostics, source).empty());
}
} // namespace Chtholly::Testing
