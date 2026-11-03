// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <span>
#include <string>

namespace Chtholly::Driver {

// Arguments exclude argv[0] and use UTF-8. Returns the compiler's status:
// 0 = success/help/version, 1 = compilation/tool/I/O failure, 2 = CLI misuse.
auto Run(std::span<const std::string> arguments) -> int;

} // namespace Chtholly::Driver
