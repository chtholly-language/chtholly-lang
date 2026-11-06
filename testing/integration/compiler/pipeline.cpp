// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Testing/frontend.h>
#include <gtest/gtest.h>

namespace Chtholly::Testing {
TEST(frontend, checked_pipeline) {
  Diagnostic::Diagnostics diagnostics;
  const auto checked = Checked("fn f(x: i32) -> i32 { return x + 2; } "
                               "fn main() -> i32 { return f(40); }",
                               diagnostics);
  ASSERT_TRUE(checked);
  const auto execution = Lower::Lower(*checked, diagnostics);
  ASSERT_TRUE(execution);
  EXPECT_EQ(execution->storage().Functions.size(), 2U);
  const auto &function = execution->storage().Functions[0];
  bool checkedAdd = false;
  for (const auto &block : function.Blocks) {
    checkedAdd |= block.End.Kind == IR::Exit::Checked &&
                  block.End.Operation == Numeric::Operation::Add;
  }
  EXPECT_TRUE(checkedAdd);
}
TEST(frontend, uninhabited_function_has_no_body) {
  Diagnostic::Diagnostics diagnostics;
  const auto checked = Checked(
      "fn f(value: !) -> i32 {} fn main() -> i32 { return 0; }", diagnostics);
  ASSERT_TRUE(checked);
  const auto execution = Lower::Lower(*checked, diagnostics);
  ASSERT_TRUE(execution);
  EXPECT_EQ(execution->storage().Functions[0].ReturnType, Types::TypeId::I32);
  EXPECT_FALSE(execution->storage().Functions[0].HasNormalEntry);
  EXPECT_TRUE(execution->storage().Functions[0].Blocks.empty());
}
TEST(frontend, canonical_termination_identity) {
  for (bool userFunction : {false, true}) {
    Diagnostic::Diagnostics diagnostics;
    const auto text = userFunction ? "fn abort() -> i32 { return 7; } "
                                     "fn main() -> i32 { return abort(); }"
                                   : "fn main() -> i32 { abort(); }";
    const auto checked = Checked(text, diagnostics);
    ASSERT_TRUE(checked);
    const auto execution = Lower::Lower(*checked, diagnostics);
    ASSERT_TRUE(execution);
    bool abortOperation = false;
    bool ordinaryCall = false;
    for (const auto &function : execution->storage().Functions) {
      for (const auto &block : function.Blocks) {
        abortOperation |= block.End.Kind == IR::Exit::Abort;
        for (const auto &instruction : block.Instructions) {
          ordinaryCall |= instruction.Code == IR::Opcode::Call;
        }
      }
    }
    EXPECT_EQ(abortOperation, !userFunction);
    EXPECT_EQ(ordinaryCall, userFunction);
  }
}
TEST(frontend, divergent_recovery_source) {
  Diagnostic::Diagnostics diagnostics;
  const auto checked =
      Checked("fn main() -> i32 { let value = abort() as u8 else { 0 }; }",
              diagnostics);
  ASSERT_TRUE(checked);
  const auto execution = Lower::Lower(*checked, diagnostics);
  ASSERT_TRUE(execution);
  const auto &function = execution->storage().Functions[0];
  ASSERT_EQ(function.Blocks.size(), 1U);
  EXPECT_EQ(function.Blocks[0].End.Kind, IR::Exit::Abort);
}
} // namespace Chtholly::Testing
