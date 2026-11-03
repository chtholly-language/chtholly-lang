// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Check/check.h>
#include <Chtholly/CodeGen/codegen.h>
#include <Chtholly/Lex/lex.h>
#include <Chtholly/Parse/parse.h>
#include <Chtholly/Testing/source.h>
#include <gtest/gtest.h>

#include <array>
#include <fstream>
#include <string>

namespace Chtholly::Testing {
TEST(codegen, windows_coff) {
  SourceFile file("fn main() -> i32 { return 42; }");
  const auto source = file.read();
  Diagnostic::Diagnostics diagnostics;
  const auto tokens = Lex::Lex(source, diagnostics);
  ASSERT_TRUE(tokens);
  const auto tree = Parse::Parse(*tokens, diagnostics);
  ASSERT_TRUE(tree);
  const auto program = Check::Check(source, *tree, diagnostics);
  ASSERT_TRUE(program);
  const auto path = file.directory() / "main.obj";
  const auto utf8 = path.u8string();
  ASSERT_TRUE(CodeGen::EmitObject(
      *program, std::string(utf8.begin(), utf8.end()), diagnostics));
  std::ifstream object(path, std::ios::binary);
  std::array<unsigned char, 20> header{};
  object.read(reinterpret_cast<char *>(header.data()),
              static_cast<std::streamsize>(header.size()));
  ASSERT_EQ(object.gcount(), 20);
  // IMAGE_FILE_MACHINE_AMD64 is 0x8664, little endian in COFF.
  EXPECT_EQ(header[0], 0x64);
  EXPECT_EQ(header[1], 0x86);
  EXPECT_GT(static_cast<unsigned>(header[2]) + 256U * header[3], 0U);
}
} // namespace Chtholly::Testing
