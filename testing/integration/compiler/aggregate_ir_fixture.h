// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/IR/ir.h>

namespace Chtholly::Testing {
// A test-only graph that source lowering does not currently need to produce:
// two owning block parameters swap on a backedge, while the other edge returns
// the original left value. Inputs arrive only when the emitted object runs.
inline auto AggregateEdgeDraft() -> IR::Draft {
  using Type = Types::TypeId;
  Types::Builder builder;
  const auto pair = builder.declare(Types::Kind::Struct, "Pair");
  builder.define(
      pair, {{"tag", Type::U8}, {"wide", Type::I128}, {"real", Type::F64}});
  std::vector<Types::Error> errors;
  IR::Draft draft;
  draft.Types = builder.finish(errors);
  draft.SourceName = "<aggregate-edge-fixture>";
  draft.Entry = 1;

  IR::Function probe;
  probe.Name = "probe";
  probe.ReturnType = pair;
  probe.Parameters = {{Type::Bool, 0}, {pair, 1}, {pair, 2}};
  probe.Locals = {Type::Bool, pair, pair};
  probe.LocalWritable = {false, false, false};
  for (IR::Id local = 0; local < probe.Locals.size(); ++local) {
    IR::Place root;
    root.Local = local;
    root.ValueType = probe.Locals[local];
    probe.LocalRoots.push_back(static_cast<IR::Id>(probe.Places.size()));
    probe.Places.push_back(root);
  }
  // 0..2: entry parameters. 3..5: loop-carried left/right/condition.
  // 6: false for the second iteration. 7: returned owning aggregate.
  probe.Values = {Type::Bool, pair,       pair,       pair,
                  pair,       Type::Bool, Type::Bool, pair};
  probe.ValuePlaces.resize(probe.Values.size(), IR::None);
  for (IR::Id value = 0; value < probe.Values.size(); ++value) {
    if (probe.Values[value] != pair) {
      continue;
    }
    IR::Place root;
    root.ValueOwner = value;
    root.ValueType = pair;
    probe.ValuePlaces[value] = static_cast<IR::Id>(probe.Places.size());
    probe.Places.push_back(root);
  }
  probe.Blocks.resize(3);
  probe.Blocks[0].Parameters = {0, 1, 2};
  IR::Instruction stop;
  stop.Result = 6;
  stop.Constant = Numeric::Value::Boolean(false);
  probe.Blocks[0].Instructions.push_back(stop);
  probe.Blocks[0].End.Kind = IR::Exit::Jump;
  probe.Blocks[0].End.First = {1, {1, 2, 0}};
  probe.Blocks[1].Parameters = {3, 4, 5};
  probe.Blocks[1].End.Kind = IR::Exit::Branch;
  probe.Blocks[1].End.Operands = {5};
  probe.Blocks[1].End.First = {1, {4, 3, 6}};
  probe.Blocks[1].End.Second = {2, {3}};
  probe.Blocks[2].Parameters = {7};
  probe.Blocks[2].End.Kind = IR::Exit::Return;
  probe.Blocks[2].End.Operands = {7};
  draft.Functions.push_back(std::move(probe));

  IR::Function main;
  main.Name = "main";
  main.ReturnType = Type::I32;
  main.Values = {Type::I32};
  main.ValuePlaces = {IR::None};
  main.Blocks.resize(1);
  IR::Instruction zero;
  zero.Result = 0;
  zero.Constant = Numeric::Value::FromBits(Type::I32, {0, 0});
  main.Blocks[0].Instructions.push_back(zero);
  main.Blocks[0].End.Kind = IR::Exit::Return;
  main.Blocks[0].End.Operands = {0};
  draft.Functions.push_back(std::move(main));
  return draft;
}
} // namespace Chtholly::Testing
