// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Check/check.h>
#include <Chtholly/CodeGen/codegen.h>
#include <Chtholly/IR/ir.h>
#include <Chtholly/Lex/lex.h>
#include <Chtholly/Lower/lower.h>
#include <Chtholly/Parse/parse.h>
#include <Chtholly/Testing/source.h>
#include <gtest/gtest.h>

#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace Chtholly::Testing {
namespace {
auto HasObjectPayload(const std::filesystem::path &path) -> bool {
  // Read the size through the stream used for object inspection. This avoids
  // clang-tidy 18's enum-range false positive in MSVC's file_size bit flags.
  std::ifstream object(path, std::ios::binary | std::ios::ate);
  return object && object.tellg() > std::streampos(20);
}

auto HasRelocation(const std::filesystem::path &path,
                   std::string_view wanted) -> bool {
  std::ifstream input(path, std::ios::binary);
  const std::vector<unsigned char> bytes(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
  const auto word = [&](std::size_t offset) -> std::uint32_t {
    return bytes.at(offset) |
           (static_cast<std::uint32_t>(bytes.at(offset + 1)) << 8);
  };
  const auto dword = [&](std::size_t offset) -> std::uint32_t {
    return word(offset) | (word(offset + 2) << 16);
  };
  const std::size_t symbols = dword(8);
  const std::size_t count = dword(12);
  const auto strings = symbols + count * 18;
  for (std::size_t symbol = 0; symbol < count;) {
    const auto record = symbols + symbol * 18;
    std::string name;
    const auto start =
        dword(record) == 0 ? strings + dword(record + 4) : record;
    const auto limit = dword(record) == 0 ? bytes.size() : record + 8;
    for (auto i = start; i < limit && bytes.at(i) != 0; ++i) {
      name += static_cast<char>(bytes[i]);
    }
    if (name == wanted && word(record + 12) == 0 &&
        bytes.at(record + 16) == 2) {
      const auto sections = word(2);
      const auto headers = 20 + word(16);
      for (std::size_t section = 0; section < sections; ++section) {
        const auto header = headers + section * 40;
        const auto relocation = dword(header + 24);
        const auto relocations = word(header + 32);
        for (std::size_t i = 0; i < relocations; ++i) {
          if (dword(relocation + i * 10 + 4) == symbol) {
            return true;
          }
        }
      }
    }
    symbol += 1 + bytes.at(record + 17);
  }
  return false;
}
} // namespace

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
  const auto execution = Lower::Lower(*program, diagnostics);
  ASSERT_TRUE(execution);
  const auto path = file.directory() / "main.obj";
  const auto utf8 = path.u8string();
  ASSERT_TRUE(CodeGen::EmitObject(
      *execution, std::string(utf8.begin(), utf8.end()), diagnostics));
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
TEST(codegen, canonical_abort_relocation) {
  for (bool shadowed : {false, true}) {
    SourceFile file(shadowed ? "fn abort() -> i32 { return 7; } fn main() -> "
                               "i32 { return abort(); }"
                             : "fn main() -> i32 { abort(); }");
    const auto source = file.read();
    Diagnostic::Diagnostics diagnostics;
    const auto tokens = Lex::Lex(source, diagnostics);
    ASSERT_TRUE(tokens);
    const auto tree = Parse::Parse(*tokens, diagnostics);
    ASSERT_TRUE(tree);
    const auto checked = Check::Check(source, *tree, diagnostics);
    ASSERT_TRUE(checked);
    const auto execution = Lower::Lower(*checked, diagnostics);
    ASSERT_TRUE(execution);
    const auto path = file.directory() / "main.obj";
    const auto utf8 = path.u8string();
    ASSERT_TRUE(CodeGen::EmitObject(
        *execution, std::string(utf8.begin(), utf8.end()), diagnostics));
    EXPECT_EQ(HasRelocation(path, "chtholly_runtime_abort"), !shadowed);
  }
}
TEST(codegen, block_storage_order_is_not_execution_order) {
  using T = Target::Type;
  for (bool reordered : {false, true}) {
    for (bool deadBlock : {false, true}) {
      IR::Draft draft;
      draft.Entry = 0;
      IR::Function function;
      function.Name = "main";
      function.ReturnType = T::I32;
      function.Values = {T::I32};
      function.Blocks.resize(deadBlock ? 4 : 3);
      const IR::Id definition = reordered ? 2 : 1;
      const IR::Id use = reordered ? 1 : 2;
      function.Blocks[0].End.Kind = IR::Exit::Jump;
      function.Blocks[0].End.First.Target = definition;
      IR::Instruction constant;
      constant.Result = 0;
      constant.Constant = Numeric::Value::FromBits(T::I32, {42, 0});
      function.Blocks[definition].Instructions.push_back(constant);
      function.Blocks[definition].End.Kind = IR::Exit::Jump;
      function.Blocks[definition].End.First.Target = use;
      function.Blocks[use].End.Kind = IR::Exit::Return;
      function.Blocks[use].End.Operands = {0};
      if (deadBlock) {
        function.Values.push_back(T::I32);
        constant.Result = 1;
        function.Blocks[3].Instructions.push_back(constant);
        function.Blocks[3].End.Kind = IR::Exit::Return;
        function.Blocks[3].End.Operands = {1};
      }
      draft.Functions.push_back(std::move(function));
      Diagnostic::Diagnostics diagnostics;
      const auto execution = IR::Verify(std::move(draft), diagnostics);
      ASSERT_TRUE(execution);
      SourceFile file("");
      const auto path = file.directory() / "main.obj";
      const auto utf8 = path.u8string();
      ASSERT_TRUE(CodeGen::EmitObject(
          *execution, std::string(utf8.begin(), utf8.end()), diagnostics));
      EXPECT_TRUE(HasObjectPayload(path));
    }
  }
}
TEST(codegen, reordered_loop_parameters_and_dead_predecessor) {
  using T = Target::Type;
  // Storage is entry, return, header, backedge, dead predecessor. The header
  // merges the entry constant and a backedge result; the return uses that
  // parameter. The dead predecessor must not become an LLVM phi incoming edge.
  IR::Draft draft;
  draft.Entry = 0;
  IR::Function function;
  function.Name = "main";
  function.ReturnType = T::I32;
  function.Values = {T::I32, T::I32, T::Bool, T::I32};
  function.Blocks.resize(5);
  const auto makeConstant = [](IR::Id result, T type, std::uint64_t bits) {
    IR::Instruction instruction;
    instruction.Result = result;
    instruction.Constant = Numeric::Value::FromBits(type, {bits, 0});
    return instruction;
  };
  function.Blocks[0].Instructions.push_back(makeConstant(0, T::I32, 42));
  function.Blocks[0].End.Kind = IR::Exit::Jump;
  function.Blocks[0].End.First = {2, {0}};
  function.Blocks[1].End.Kind = IR::Exit::Return;
  function.Blocks[1].End.Operands = {1};
  function.Blocks[2].Parameters = {1};
  function.Blocks[2].Instructions.push_back(makeConstant(2, T::Bool, 0));
  function.Blocks[2].End.Kind = IR::Exit::Branch;
  function.Blocks[2].End.Operands = {2};
  function.Blocks[2].End.First.Target = 3;
  function.Blocks[2].End.Second.Target = 1;
  IR::Instruction backedgeValue;
  backedgeValue.Code = IR::Opcode::Unary;
  backedgeValue.Result = 3;
  backedgeValue.Operation = Numeric::Operation::Plus;
  backedgeValue.Operands = {1};
  function.Blocks[3].Instructions.push_back(backedgeValue);
  function.Blocks[3].End.Kind = IR::Exit::Jump;
  function.Blocks[3].End.First = {2, {3}};
  function.Blocks[4].End.Kind = IR::Exit::Jump;
  function.Blocks[4].End.First = {2, {0}};
  draft.Functions.push_back(std::move(function));
  Diagnostic::Diagnostics diagnostics;
  const auto execution = IR::Verify(std::move(draft), diagnostics);
  ASSERT_TRUE(execution);
  SourceFile file("");
  const auto path = file.directory() / "main.obj";
  const auto utf8 = path.u8string();
  ASSERT_TRUE(CodeGen::EmitObject(
      *execution, std::string(utf8.begin(), utf8.end()), diagnostics));
  EXPECT_TRUE(HasObjectPayload(path));
}

TEST(codegen, branch_edges_to_same_block_keep_arguments) {
  using T = Target::Type;
  for (bool condition : {false, true}) {
    for (bool sameArguments : {false, true}) {
      IR::Draft draft;
      draft.Entry = 0;
      IR::Function function;
      function.Name = "main";
      function.ReturnType = T::I32;
      function.Values = {T::Bool, T::I32, T::I32, T::I32};
      function.Blocks.resize(2);
      const auto constant = [](IR::Id id, Numeric::Value value) {
        IR::Instruction result;
        result.Result = id;
        result.Constant = std::move(value);
        return result;
      };
      function.Blocks[0].Instructions = {
          constant(0, Numeric::Value::Boolean(condition)),
          constant(1, Numeric::Value::FromBits(T::I32, {41, 0})),
          constant(2, Numeric::Value::FromBits(T::I32, {42, 0}))};
      function.Blocks[0].End.Kind = IR::Exit::Branch;
      function.Blocks[0].End.Operands = {0};
      function.Blocks[0].End.First = {1, {1}};
      function.Blocks[0].End.Second = {1, {sameArguments ? 1U : 2U}};
      function.Blocks[1].Parameters = {3};
      function.Blocks[1].End.Kind = IR::Exit::Return;
      function.Blocks[1].End.Operands = {3};
      draft.Functions.push_back(std::move(function));
      Diagnostic::Diagnostics diagnostics;
      const auto execution = IR::Verify(std::move(draft), diagnostics);
      ASSERT_TRUE(execution);
      SourceFile file("");
      const auto path = file.directory() / "main.obj";
      const auto utf8 = path.u8string();
      ASSERT_TRUE(CodeGen::EmitObject(
          *execution, std::string(utf8.begin(), utf8.end()), diagnostics));
      EXPECT_TRUE(HasObjectPayload(path));
    }
  }
}
} // namespace Chtholly::Testing
