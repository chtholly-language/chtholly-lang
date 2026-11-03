// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace Chtholly::Source {
class Buffer;
}

namespace Chtholly::Diagnostic {

class Diagnostics {
public:
  auto sourceError(std::size_t offset, std::string message) -> void;
  auto error(std::string stage, std::string message) -> void;
  auto render(std::ostream &output,
              const Source::Buffer *source = nullptr) const -> void;

private:
  struct Record {
    std::optional<std::size_t> Offset;
    std::string Stage;
    std::string Message;
  };
  std::vector<Record> Records;
};

} // namespace Chtholly::Diagnostic
