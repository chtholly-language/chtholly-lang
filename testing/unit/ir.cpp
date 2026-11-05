// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Testing/frontend.h>
#include <gtest/gtest.h>

namespace Chtholly::Testing {
namespace {
auto Baseline() -> IR::Draft {
  Diagnostic::Diagnostics diagnostics;
  const auto checked =
      Checked("fn f(x: i32) -> i32 { if (x == 0) { return 1; } return x + 1; } "
              "fn main() -> i32 { return f(41); }",
              diagnostics);
  if (!checked) {
    throw std::logic_error("invalid IR fixture source");
  }
  const auto program = Lower::Lower(*checked, diagnostics);
  if (!program) {
    throw std::logic_error("invalid IR fixture lowering");
  }
  return program->storage();
}
} // namespace
TEST(ir, accepts_typed_control_flow) {
  Diagnostic::Diagnostics diagnostics;
  EXPECT_TRUE(IR::Verify(Baseline(), diagnostics));
}
TEST(ir, rejects_corrupted_records) {
  for (unsigned damage = 0; damage < 8; ++damage) {
    auto draft = Baseline();
    auto &function = draft.Functions[0];
    switch (damage) {
    case 0:
      draft.Entry = IR::None;
      break;
    case 1:
      function.Blocks[0].End.Kind = IR::Exit::Missing;
      break;
    case 2:
      function.Blocks[0].Parameters[0] = IR::None;
      break;
    case 3:
      function.Values[function.Blocks[0].Parameters[0]] = Target::Type::Bool;
      break;
    case 4:
      function.Blocks[0].End.First.Target = IR::None;
      break;
    case 5:
      function.HasNormalEntry = false;
      break;
    case 6:
      function.Blocks[0].Instructions[0].Code = static_cast<IR::Opcode>(255);
      break;
    case 7:
      function.Blocks[0].End.Kind = static_cast<IR::Exit>(255);
      break;
    }
    Diagnostic::Diagnostics diagnostics;
    EXPECT_FALSE(IR::Verify(std::move(draft), diagnostics)) << damage;
  }
}
TEST(ir, rejects_wrong_join_and_undefined_use) {
  auto draft = Baseline();
  auto &function = draft.Functions[0];
  bool changed = false;
  for (auto &block : function.Blocks) {
    if (block.End.Kind == IR::Exit::Checked) {
      function.Values[function.Blocks[block.End.First.Target].Parameters[0]] =
          Target::Type::Bool;
      changed = true;
      break;
    }
  }
  ASSERT_TRUE(changed);
  Diagnostic::Diagnostics diagnostics;
  EXPECT_FALSE(IR::Verify(std::move(draft), diagnostics));
  draft = Baseline();
  draft.Functions[0].Values.push_back(Target::Type::I32);
  EXPECT_FALSE(IR::Verify(std::move(draft), diagnostics));
}
} // namespace Chtholly::Testing
