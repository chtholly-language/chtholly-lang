// SPDX-License-Identifier: MIT OR Apache-2.0
#include "emission.h"
#include <Chtholly/CodeGen/test_adapter.h>

namespace Chtholly::CodeGen::Testing {
auto EmitFunctions(const IR::Program &program, std::string_view objectPath,
                   Diagnostic::Diagnostics &diagnostics) -> bool {
  return EmitObjectFile(program, objectPath, diagnostics, false);
}
} // namespace Chtholly::CodeGen::Testing
