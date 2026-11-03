// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Source/source.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>
#include <utility>

namespace Chtholly::Source {

Buffer::Buffer(std::string name, std::string text)
    : Name(std::move(name)), Text(std::move(text)), LineStarts{0} {
  for (std::size_t i = 0; i < Text.size(); ++i) {
    if (Text[i] == '\r') {
      if (i + 1 < Text.size() && Text[i + 1] == '\n') {
        ++i;
      }
      LineStarts.push_back(i + 1);
    } else if (Text[i] == '\n') {
      LineStarts.push_back(i + 1);
    }
  }
}

auto Buffer::Read(const std::filesystem::path &path,
                  std::string &error) -> std::optional<Buffer> {
  error.clear();
  std::error_code statusError;
  if (!std::filesystem::is_regular_file(path, statusError)) {
    error = statusError ? statusError.message() : "input is not a regular file";
    return std::nullopt;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "cannot open input for reading";
    return std::nullopt;
  }
  std::string text;
  std::array<char, 4096> chunk;
  while (input) {
    input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    text.append(chunk.data(), static_cast<std::size_t>(input.gcount()));
  }
  if (input.bad() || !input.eof()) {
    error = "failed while reading input";
    return std::nullopt;
  }
  const auto name = path.u8string();
  return Buffer(std::string(name.begin(), name.end()), std::move(text));
}

auto Buffer::slice(Span span) const -> std::string_view {
  return std::string_view(Text).substr(span.Begin, span.Length);
}

auto Buffer::locate(std::size_t offset) const -> Location {
  offset = std::min(offset, Text.size());
  const auto after =
      std::upper_bound(LineStarts.begin(), LineStarts.end(), offset);
  const auto index = static_cast<std::size_t>(after - LineStarts.begin() - 1);
  return {index + 1, offset - LineStarts[index] + 1};
}

} // namespace Chtholly::Source
