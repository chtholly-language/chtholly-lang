// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <string_view>

namespace Chtholly::SemIR {
class Program;
}
namespace Chtholly::Diagnostic {
class Diagnostics;
}

namespace Chtholly::CodeGen {

// Writes only the caller-owned temporary object path (UTF-8). Failure may
// leave a partial object there; Driver owns cleanup and final publication.
auto EmitObject(const SemIR::Program &program, std::string_view objectPath,
                Diagnostic::Diagnostics &diagnostics) -> bool;

} // namespace Chtholly::CodeGen
