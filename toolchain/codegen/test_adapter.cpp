// SPDX-License-Identifier: MIT OR Apache-2.0
#include "emission.h"
#include <Chtholly/CodeGen/test_adapter.h>
#include <Chtholly/Flow/flow.h>

namespace Chtholly::CodeGen::Testing {
auto EmitFunctions(const Flow::InitializationCheckedView &program,
                   std::string_view objectPath,
                   Diagnostic::Diagnostics &diagnostics) -> bool {
  return EmitObjectFile(program, objectPath, diagnostics, false);
}
} // namespace Chtholly::CodeGen::Testing
