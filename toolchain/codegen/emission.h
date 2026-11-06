// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <string_view>

namespace Chtholly::Flow {
class InitializationCheckedView;
}
namespace Chtholly::Diagnostic {
class Diagnostics;
}

namespace Chtholly::CodeGen {
// Shared implementation for the product entry and the separately linked
// test adapter. This header is private to CodeGen.
auto EmitObjectFile(const Flow::InitializationCheckedView &program,
                    std::string_view objectPath,
                    Diagnostic::Diagnostics &diagnostics,
                    bool nativeEntry) -> bool;
} // namespace Chtholly::CodeGen
