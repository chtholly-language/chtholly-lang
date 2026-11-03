// SPDX-License-Identifier: MIT OR Apache-2.0
#include "entry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

auto FixtureMain(int argc, char **argv) -> int {
  const auto executable = std::filesystem::u8path(argv[0]);
  std::ifstream settings(executable.parent_path() / "mode.txt");
  std::string mode;
  settings >> mode;
  if (argc == 2 && std::string(argv[1]) == "--version") {
    const bool clang = executable.filename() == "clang-cl.exe";
    const bool mismatch = (clang && mode == "clang-version-mismatch") ||
                          (!clang && mode == "linker-version-mismatch");
    const auto version = mismatch ? "17.0.0" : "18.1.8";
    std::cout << (clang ? "clang version " : "LLD ") << version << '\n';
    return 0;
  }
  if (mode == "link-success-without-output") {
    return 0;
  }
  std::cerr << "controlled link fixture failure\n";
  return 23;
}

#ifdef _WIN32
auto wmain(int argc, wchar_t **argv) -> int {
  return Chtholly::Testing::WindowsFixtureMain(argc, argv, FixtureMain);
}
#else
auto main(int argc, char **argv) -> int { return FixtureMain(argc, argv); }
#endif
