// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/IR/ir.h>
#include <llvm/IR/Module.h>
namespace Chtholly::CodeGen {
auto EmitModule(const IR::Draft &program, llvm::Module &module,
                bool nativeEntry) -> void;
} // namespace Chtholly::CodeGen
