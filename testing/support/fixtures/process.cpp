// SPDX-License-Identifier: MIT OR Apache-2.0
#include "entry.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
auto Pause() -> void {
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}
auto Heartbeat(const std::filesystem::path &path) -> void {
  for (;;) {
    std::ofstream output(path, std::ios::app);
    output << '.';
    output.close();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
}
} // namespace

auto FixtureMain(int argc, char **argv) -> int {
  const std::string operation = argc > 1 ? argv[1] : "run";
  if (operation == "--hang") {
    Pause();
  }
  if (operation == "--heartbeat" && argc == 3) {
    Heartbeat(std::filesystem::u8path(argv[2]));
  }
  if ((operation == "--tree" || operation == "--orphan") && argc == 3) {
#ifdef _WIN32
    const auto program =
        std::filesystem::absolute(std::filesystem::u8path(argv[0])).wstring();
    auto command = L"\"" + program + L"\" --heartbeat \"" +
                   std::filesystem::u8path(argv[2]).wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &info)) {
      return 3;
    }
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
#else
    const auto pid = fork();
    if (pid == -1) {
      return 3;
    }
    if (pid == 0) {
      execl(argv[0], argv[0], "--heartbeat", argv[2],
            static_cast<char *>(nullptr));
      _exit(3);
    }
#endif
    if (operation == "--orphan") {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(3);
      while (!std::filesystem::exists(std::filesystem::u8path(argv[2]))) {
        if (std::chrono::steady_clock::now() >= deadline) {
          return 3;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      return 0;
    }
    Pause();
  }
  if (operation == "--bytes" && argc == 3) {
    const auto size = static_cast<std::size_t>(std::stoul(argv[2]));
    std::cout << std::string(size / 2, 'x');
    std::cerr << std::string(size - size / 2, 'y');
    return 0;
  }
  if (operation == "--flood") {
    for (;;) {
      std::cout << std::string(8192, 'x') << std::flush;
    }
  }
  if (operation == "--crash") {
#ifdef _WIN32
    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    RaiseException(0xC0000005, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#else
    std::abort();
#endif
    return 3;
  }
  if (argc == 4 && std::string(argv[2]) == "-o") {
    const auto *value = std::getenv("CHTHOLLY_FIXTURE_MODE");
    const std::string mode = value ? value : "success";
    if (mode == "hang") {
      Pause();
    }
    if (mode == "crash") {
#ifdef _WIN32
      SetErrorMode(SEM_NOGPFAULTERRORBOX);
      RaiseException(0xC0000005, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#else
      std::abort();
#endif
    }
    if (mode == "reject" || mode == "bad-diagnostic") {
      std::cerr << (mode == "reject" ? "input.cns:1:1: error: rejected\n"
                                     : "unrelated\n");
      return 1;
    }
    if (mode == "tool-error") {
      std::cerr << "chthollyc: toolchain: error: missing tool clang-cl.exe\n";
      return 1;
    }
    if (mode == "no-output") {
      return 0;
    }
    std::error_code error;
    std::filesystem::copy_file(std::filesystem::u8path(argv[0]),
                               std::filesystem::u8path(argv[3]), error);
    if (error) {
      return 3;
    }
#ifdef _WIN32
    const auto runtime = std::filesystem::u8path(argv[0]).parent_path() /
                         "clang_rt.asan_dynamic-x86_64.dll";
    const auto runtimeStatus = std::filesystem::status(runtime, error);
    if (error == std::errc::no_such_file_or_directory) {
      error.clear();
    }
    if (error) {
      return 3;
    }
    if (std::filesystem::is_regular_file(runtimeStatus)) {
      std::filesystem::copy_file(
          runtime,
          std::filesystem::u8path(argv[3]).parent_path() / runtime.filename(),
          std::filesystem::copy_options::overwrite_existing, error);
    }
#endif
    return error ? 3 : 0;
  }
  if (const auto *marker = std::getenv("CHTHOLLY_RUN_MARKER")) {
    std::ofstream output(std::filesystem::u8path(marker));
    output << "ran";
  }
  if (const auto *mode = std::getenv("CHTHOLLY_FIXTURE_MODE")) {
    const std::string value(mode);
    if (value == "run-crash") {
      std::abort();
    }
    if (value == "run-timeout") {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
    if (value == "run-abort") {
      return 121;
    }
    if (value == "run-sanitizer") {
      std::cerr << "AddressSanitizer: fixture report\n";
      return 120;
    }
    if (value == "run-language-error" || value == "run-wrong-category" ||
        value == "run-index-error") {
      const auto source =
          std::filesystem::u8path(argv[0]).parent_path().parent_path() /
          "input" / "input.cns";
      const auto utf8 = source.u8string();
      std::cerr << std::string(utf8.begin(), utf8.end())
                << ":2:3: chtholly failure["
                << (value == "run-index-error" ? "Index.OutOfBounds"
                    : value == "run-language-error"
                        ? "Arithmetic.DivisionByZero"
                        : "Arithmetic.Overflow")
                << "]\n";
      return 120;
    }
  }
  return 42;
}

#ifdef _WIN32
auto wmain(int argc, wchar_t **argv) -> int {
  return Chtholly::Testing::WindowsFixtureMain(argc, argv, FixtureMain);
}
#else
auto main(int argc, char **argv) -> int { return FixtureMain(argc, argv); }
#endif
