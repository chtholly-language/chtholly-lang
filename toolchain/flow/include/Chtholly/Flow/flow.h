// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/IR/ir.h>
#include <cstddef>
#include <optional>

namespace Chtholly::Diagnostic {
class Diagnostics;
}
namespace Chtholly::Flow {
struct Limits {
  std::size_t Places = 100000;
  std::size_t StateCells = 10000000;
  std::size_t FactChanges = 10000000;
};
// Owns the exact immutable, structurally verified IR that was analyzed.
// There is no constructor from a draft or a naked storage pointer.
class InitializationCheckedView {
public:
  auto program() const -> const IR::Program & { return Program; }
  auto storage() const -> const IR::Draft & { return Program.storage(); }

private:
  friend auto Analyze(IR::Program, Diagnostic::Diagnostics &,
                      Limits) -> std::optional<InitializationCheckedView>;
  explicit InitializationCheckedView(IR::Program program)
      : Program(std::move(program)) {}
  IR::Program Program;
};
auto Analyze(IR::Program program, Diagnostic::Diagnostics &diagnostics,
             Limits limits = {}) -> std::optional<InitializationCheckedView>;
} // namespace Chtholly::Flow
