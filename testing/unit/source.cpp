// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Source/source.h>
#include <Chtholly/Testing/source.h>

#include <gtest/gtest.h>

#include <string>

namespace Chtholly::Testing {
namespace {

TEST(source, empty_eof) {
  SourceFile file("");
  const auto source = file.read();
  EXPECT_TRUE(source.text().empty());
  EXPECT_EQ(source.locate(0).Line, 1U);
  EXPECT_EQ(source.locate(0).Column, 1U);
}

TEST(source, line_endings) {
  SourceFile file("a\r\nb\nc\rd");
  const auto source = file.read();
  EXPECT_EQ(source.locate(3).Line, 2U);
  EXPECT_EQ(source.locate(5).Line, 3U);
  EXPECT_EQ(source.locate(7).Line, 4U);
  EXPECT_EQ(source.locate(8).Column, 2U);
}

TEST(source, byte_columns) {
  SourceFile file("\xEF\xBB\xBF"
                  "// \xC3\xA9\nfn");
  const auto source = file.read();
  EXPECT_EQ(source.locate(3).Column, 4U);
  EXPECT_EQ(source.locate(9).Column, 1U);
  EXPECT_EQ(source.locate(10).Column, 2U);
}

TEST(source, read_failure) {
  SourceFile file("");
  std::string error;
  EXPECT_FALSE(Source::Buffer::Read(file.directory() / "missing.cns", error));
  EXPECT_FALSE(error.empty());
}

} // namespace
} // namespace Chtholly::Testing
