// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <string>
#include <string_view>

namespace Chtholly::Flow {
class InitializationCheckedView;
}
namespace Chtholly::Diagnostic {
class Diagnostics;
}

namespace Chtholly::CodeGen {

// Writes only the caller-owned temporary object path (UTF-8). Failure may
// leave a partial object there; Driver owns cleanup and final publication.
auto EmitObject(const Flow::InitializationCheckedView &program,
                std::string_view objectPath,
                Diagnostic::Diagnostics &diagnostics) -> bool;

auto FunctionSymbol(unsigned function) -> std::string;

} // namespace Chtholly::CodeGen
