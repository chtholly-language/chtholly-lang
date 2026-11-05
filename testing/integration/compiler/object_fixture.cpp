// SPDX-License-Identifier: MIT OR Apache-2.0
// Test-only object producer. All language checking uses production interfaces.
#include "entry.h"

#include <Chtholly/CodeGen/codegen.h>
#include <Chtholly/CodeGen/test_adapter.h>
#include <Chtholly/Testing/frontend.h>

#include <iostream>

auto FixtureMain(int argc, char **argv) -> int {
  if (argc != 3) {
    return 2;
  }
  std::string error;
  const auto source =
      Chtholly::Source::Buffer::Read(std::filesystem::u8path(argv[1]), error);
  if (!source) {
    std::cerr << error;
    return 2;
  }
  Chtholly::Diagnostic::Diagnostics diagnostics;
  const auto fail = [&] {
    diagnostics.render(std::cerr, &*source);
    return 1;
  };
  const auto tokens = Chtholly::Lex::Lex(*source, diagnostics);
  if (!tokens) {
    return fail();
  }
  const auto tree = Chtholly::Parse::Parse(*tokens, diagnostics);
  if (!tree) {
    return fail();
  }
  const auto checked = Chtholly::Check::Check(*source, *tree, diagnostics);
  if (!checked) {
    return fail();
  }
  const auto execution = Chtholly::Lower::Lower(*checked, diagnostics);
  if (!execution) {
    return fail();
  }
  if (!Chtholly::CodeGen::Testing::EmitFunctions(*execution, argv[2],
                                                 diagnostics)) {
    return fail();
  }
  return 0;
}

auto wmain(int argc, wchar_t **argv) -> int {
  return Chtholly::Testing::WindowsFixtureMain(argc, argv, FixtureMain);
}
