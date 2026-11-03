// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once

#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/Source/source.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Chtholly::Testing {

class SourceFile {
public:
  explicit SourceFile(std::string_view text) {
    static std::atomic<unsigned> sequence = 0;
    const auto root = std::filesystem::temp_directory_path();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
      const auto stamp =
          std::chrono::steady_clock::now().time_since_epoch().count();
      Directory = root / ("chtholly-test-" + std::to_string(stamp) + "-" +
                          std::to_string(sequence.fetch_add(1)));
      std::error_code error;
      if (std::filesystem::create_directory(Directory, error)) {
        Path = Directory / "input.cns";
        std::ofstream output(Path, std::ios::binary);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        if (!output) {
          std::error_code ignored;
          std::filesystem::remove(Path, ignored);
          std::filesystem::remove(Directory, ignored);
          throw std::runtime_error("cannot write fixture source");
        }
        return;
      }
      if (error) {
        throw std::system_error(error);
      }
    }
    throw std::runtime_error("cannot allocate fixture directory");
  }
  SourceFile(const SourceFile &) = delete;
  auto operator=(const SourceFile &) -> SourceFile & = delete;
  ~SourceFile() {
    std::error_code ignored;
    std::filesystem::remove(Path, ignored);
    std::filesystem::remove(Directory / "main.obj", ignored);
    std::filesystem::remove(Directory, ignored);
  }
  auto read() const -> Source::Buffer {
    std::string error;
    auto source = Source::Buffer::Read(Path, error);
    if (!source) {
      throw std::runtime_error(error);
    }
    return std::move(*source);
  }
  auto directory() const -> const std::filesystem::path & { return Directory; }

private:
  std::filesystem::path Directory;
  std::filesystem::path Path;
};

inline auto Render(const Diagnostic::Diagnostics &diagnostics,
                   const Source::Buffer &source) -> std::string {
  std::ostringstream output;
  diagnostics.render(output, &source);
  return output.str();
}

} // namespace Chtholly::Testing
