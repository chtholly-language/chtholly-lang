// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/Source/source.h>

#include <ostream>
#include <utility>

namespace Chtholly::Diagnostic {

auto Diagnostics::sourceError(std::size_t offset, std::string message) -> void {
  Records.push_back({offset, {}, std::move(message)});
}

auto Diagnostics::error(std::string stage, std::string message) -> void {
  Records.push_back({std::nullopt, std::move(stage), std::move(message)});
}

auto Diagnostics::render(std::ostream &output, const Source::Buffer *source) const
    -> void {
  for (const auto &record : Records) {
    if (record.Offset && source) {
      const auto location = source->locate(*record.Offset);
      output << source->name() << ':' << location.Line << ':' << location.Column;
    } else {
      output << "chthollyc";
      if (!record.Stage.empty()) {
        output << ": " << record.Stage;
      }
    }
    output << ": error: " << record.Message << '\n';
  }
}

} // namespace Chtholly::Diagnostic
