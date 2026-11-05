// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <string_view>

namespace Chtholly::IR {
class Program;
}
namespace Chtholly::Diagnostic {
class Diagnostics;
}
namespace Chtholly::CodeGen::Testing {
// Defined only by the BUILD_TESTING adapter library, never by the product.
auto EmitFunctions(const IR::Program &program, std::string_view objectPath,
                   Diagnostic::Diagnostics &diagnostics) -> bool;
} // namespace Chtholly::CodeGen::Testing
