// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Chtholly::Source {

struct Span {
  std::size_t Begin;
  std::size_t Length;
};

struct Location {
  std::size_t Line;
  std::size_t Column;
};

// Owns all text. Spans are byte offsets into this specific buffer, including
// any initial BOM. Tokens and trees must only be used with their source buffer.
class Buffer {
public:
  static auto Read(const std::filesystem::path &path,
                   std::string &error) -> std::optional<Buffer>;

  auto name() const -> const std::string & { return Name; }
  auto text() const -> std::string_view { return Text; }
  auto slice(Span span) const -> std::string_view;
  auto locate(std::size_t offset) const -> Location;

private:
  Buffer(std::string name, std::string text);
  std::string Name;
  std::string Text;
  std::vector<std::size_t> LineStarts;
};

} // namespace Chtholly::Source
