// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Driver/driver.h>

#include <windows.h>

#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

auto UTF8(std::wstring_view argument) -> std::string {
  if (argument.empty()) {
    return {};
  }
  if (argument.size() >
      static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    throw std::runtime_error("command-line argument is too long");
  }
  const auto length = static_cast<int>(argument.size());
  const int bytes =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument.data(),
                          length, nullptr, 0, nullptr, nullptr);
  if (bytes == 0) {
    throw std::runtime_error("command line contains invalid UTF-16");
  }
  std::string result(static_cast<std::size_t>(bytes), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument.data(),
                          length, result.data(), bytes, nullptr,
                          nullptr) != bytes) {
    throw std::runtime_error("cannot convert command line to UTF-8");
  }
  return result;
}

} // namespace

auto wmain(int argc, wchar_t **argv) -> int {
  try {
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
      arguments.push_back(UTF8(argv[i]));
    }
    return Chtholly::Driver::Run(arguments);
  } catch (const std::exception &error) {
    std::cerr << "chthollyc: error: " << error.what() << '\n';
    return 1;
  }
}
