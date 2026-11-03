// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once

#ifdef _WIN32
#include <windows.h>

#include <cstddef>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Chtholly::Testing {

template <typename Callback>
auto WindowsFixtureMain(int argc, wchar_t **argv, Callback callback) -> int {
  try {
    std::vector<std::string> storage;
    storage.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
      const std::wstring_view argument(argv[i]);
      if (argument.size() >
          static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("fixture argument exceeds Win32 size");
      }
      const auto length = static_cast<int>(argument.size());
      if (length == 0) {
        storage.emplace_back();
        continue;
      }
      const int size =
          WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument.data(),
                              length, nullptr, 0, nullptr, nullptr);
      if (size == 0) {
        throw std::runtime_error("invalid UTF-16 fixture argument");
      }
      std::string value(static_cast<std::size_t>(size), '\0');
      if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument.data(),
                              length, value.data(), size, nullptr,
                              nullptr) != size) {
        throw std::runtime_error("fixture UTF-8 conversion failed");
      }
      storage.push_back(std::move(value));
    }
    std::vector<char *> arguments;
    for (auto &argument : storage) {
      arguments.push_back(argument.data());
    }
    return callback(argc, arguments.data());
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 3;
  }
}

} // namespace Chtholly::Testing
#endif
