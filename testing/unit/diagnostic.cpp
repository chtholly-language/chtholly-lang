// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/Testing/source.h>

#include <gtest/gtest.h>

#include <string>

namespace Chtholly::Testing {
namespace {

TEST(diagnostic, source_and_tool_errors) {
  SourceFile file("a\nxyz");
  const auto source = file.read();
  Diagnostic::Diagnostics diagnostics;
  diagnostics.sourceError(3, "reason");
  diagnostics.error("link", "tool failed");
  const auto text = Render(diagnostics, source);
  EXPECT_NE(text.find(":2:2: error: reason\n"), std::string::npos);
  EXPECT_NE(text.find("chthollyc: link: error: tool failed\n"),
            std::string::npos);
}

} // namespace
} // namespace Chtholly::Testing
