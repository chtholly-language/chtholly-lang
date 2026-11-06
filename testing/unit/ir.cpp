// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Testing/frontend.h>
#include <gtest/gtest.h>
#include <type_traits>
#include <utility>

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
      function.Values[function.Blocks[0].Parameters[0]] = Types::TypeId::Bool;
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
          Types::TypeId::Bool;
      changed = true;
      break;
    }
  }
  ASSERT_TRUE(changed);
  Diagnostic::Diagnostics diagnostics;
  EXPECT_FALSE(IR::Verify(std::move(draft), diagnostics));
  draft = Baseline();
  draft.Functions[0].Values.push_back(Types::TypeId::I32);
  EXPECT_FALSE(IR::Verify(std::move(draft), diagnostics));
}
TEST(ir, rejects_foreign_types_and_invalid_projections) {
  auto draft = Baseline();
  draft.Types.reset();
  Diagnostic::Diagnostics diagnostics;
  EXPECT_FALSE(IR::Verify(draft, diagnostics));
  draft = Baseline();
  Types::Builder foreign;
  const auto foreignType = foreign.declare(Types::Kind::Struct, "Foreign");
  foreign.define(foreignType, {});
  draft.Functions[0].Locals[0] = foreignType;
  EXPECT_FALSE(IR::Verify(draft, diagnostics));
  draft = Baseline();
  draft.Functions[0].Places[0].Parent = 0;
  EXPECT_FALSE(IR::Verify(draft, diagnostics));
  draft = Baseline();
  draft.Functions[0].Places[0].Writable = true;
  EXPECT_FALSE(IR::Verify(draft, diagnostics));
}
TEST(ir, analysis_proof_owns_immutable_version) {
  static_assert(
      !std::is_constructible_v<Flow::InitializationCheckedView, IR::Draft>);
  static_assert(
      !std::is_constructible_v<Flow::InitializationCheckedView, IR::Program>);
  using AnalyzedStorage =
      decltype(std::declval<Flow::InitializationCheckedView>().storage());
  static_assert(std::is_const_v<std::remove_reference_t<AnalyzedStorage>>);
  Diagnostic::Diagnostics diagnostics;
  auto draft = Baseline();
  const auto verified = IR::Verify(draft, diagnostics);
  ASSERT_TRUE(verified);
  const auto analyzed = Flow::Analyze(*verified, diagnostics);
  ASSERT_TRUE(analyzed);
  draft.Functions[0].HasNormalEntry = false;
  EXPECT_FALSE(IR::Verify(draft, diagnostics));
  EXPECT_TRUE(analyzed->storage().Functions[0].HasNormalEntry);
}
TEST(ir, rejects_duplicate_logical_positions) {
  Diagnostic::Diagnostics diagnostics;
  const auto checked =
      Checked("fn probe(_ pair: (i32, i32)) -> i32 { return pair.0; } "
              "fn main() -> i32 { return probe((1, 2)); }",
              diagnostics);
  ASSERT_TRUE(checked);
  const auto program = Lower::Lower(*checked, diagnostics);
  ASSERT_TRUE(program);
  auto draft = program->storage();
  auto &places = draft.Functions[0].Places;
  std::optional<IR::Place> duplicate;
  for (const auto &place : places) {
    if (place.Form == IR::Projection::Field) {
      duplicate = place;
      break;
    }
  }
  ASSERT_TRUE(duplicate);
  places.push_back(*duplicate);
  EXPECT_FALSE(IR::Verify(std::move(draft), diagnostics));
}
TEST(ir, rejects_nondominating_branch_value) {
  Diagnostic::Diagnostics diagnostics;
  const auto checked =
      Checked("fn probe(_ condition: bool) -> i32 { "
              "if (condition) { return 1; } else { return 2; } } "
              "fn main() -> i32 { return probe(true); }",
              diagnostics);
  ASSERT_TRUE(checked);
  const auto program = Lower::Lower(*checked, diagnostics);
  ASSERT_TRUE(program);
  auto draft = program->storage();
  std::vector<IR::Terminator *> returns;
  for (auto &block : draft.Functions[0].Blocks) {
    if (block.End.Kind == IR::Exit::Return) {
      returns.push_back(&block.End);
    }
  }
  ASSERT_EQ(returns.size(), 2U);
  returns[1]->Operands = returns[0]->Operands;
  EXPECT_FALSE(IR::Verify(std::move(draft), diagnostics));
}
TEST(ir, rejects_dynamic_index_without_its_bounds_proof) {
  Diagnostic::Diagnostics diagnostics;
  const auto checked = Checked(
      "fn probe(_ index: usize) -> i32 { let a = [1, 2]; return a[index]; } "
      "fn main() -> i32 { return probe(0); }",
      diagnostics);
  ASSERT_TRUE(checked);
  const auto program = Lower::Lower(*checked, diagnostics);
  ASSERT_TRUE(program);
  auto draft = program->storage();
  auto &function = draft.Functions[0];
  bool damaged = false;
  for (auto &place : function.Places) {
    if (place.Form == IR::Projection::DynamicElement) {
      // A correctly typed, dominating parameter is still an unchecked index.
      place.IndexValue = function.Blocks[0].Parameters[0];
      damaged = true;
    }
  }
  ASSERT_TRUE(damaged);
  EXPECT_FALSE(IR::Verify(std::move(draft), diagnostics));
}
} // namespace Chtholly::Testing
