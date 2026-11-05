// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/CodeGen/codegen.h>

#include "emission.h"
#include "emitter.h"

#include <Chtholly/Diagnostic/diagnostic.h>

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>

#include <stdexcept>

namespace Chtholly::CodeGen {
auto FunctionSymbol(unsigned function) -> std::string {
  return "__chtholly_fn_" + std::to_string(function);
}

auto EmitObjectFile(const IR::Program &program, std::string_view path,
                    Diagnostic::Diagnostics &diagnostics,
                    bool nativeEntry) -> bool {
  static const bool initialized = [] {
    return !llvm::InitializeNativeTarget() &&
           !llvm::InitializeNativeTargetAsmPrinter();
  }();
  if (!initialized) {
    diagnostics.error("code generation", "cannot initialize X86 target");
    return false;
  }
  const auto &input = program.storage();
  std::string error;
  const auto triple = std::string(input.Target.Triple);
  const auto *target = llvm::TargetRegistry::lookupTarget(triple, error);
  if (!target) {
    diagnostics.error("code generation", error);
    return false;
  }
  llvm::TargetOptions options;
  std::unique_ptr<llvm::TargetMachine> machine(
      target->createTargetMachine(triple, "generic", "", options, std::nullopt,
                                  std::nullopt, llvm::CodeGenOptLevel::None));
  if (!machine) {
    diagnostics.error("code generation", "cannot create target machine");
    return false;
  }
  llvm::LLVMContext context;
  llvm::Module module("chtholly-core", context);
  module.setTargetTriple(triple);
  module.setDataLayout(machine->createDataLayout());
  const auto &layout = module.getDataLayout();
  if (layout.getPointerSizeInBits() != input.Target.PointerBits ||
      layout.isLittleEndian() != input.Target.LittleEndian) {
    diagnostics.error("target",
                      "LLVM data layout disagrees with target description");
    return false;
  }
  for (auto type : {Target::Type::I8, Target::Type::I16, Target::Type::I32,
                    Target::Type::I64, Target::Type::I128}) {
    auto *integerType =
        llvm::IntegerType::get(context, input.Target.bits(type));
    if (layout.getABITypeAlign(integerType).value() !=
        input.Target.alignment(type)) {
      diagnostics.error(
          "target", "LLVM scalar alignment disagrees with target description");
      return false;
    }
  }
  try {
    EmitModule(input, module, nativeEntry);
  } catch (const std::logic_error &error) {
    diagnostics.error("code generation", error.what());
    return false;
  }
  std::string verification;
  llvm::raw_string_ostream stream(verification);
  if (llvm::verifyModule(module, &stream)) {
    diagnostics.error("LLVM IR verification", verification);
    return false;
  }
  std::error_code fileError;
  llvm::raw_fd_ostream output(llvm::StringRef(path.data(), path.size()),
                              fileError, llvm::sys::fs::OF_None);
  if (fileError) {
    diagnostics.error("object output", fileError.message());
    return false;
  }
  llvm::legacy::PassManager passes;
  if (machine->addPassesToEmitFile(passes, output, nullptr,
                                   llvm::CodeGenFileType::ObjectFile)) {
    diagnostics.error("code generation", "COFF emission unavailable");
    return false;
  }
  passes.run(module);
  output.close();
  if (output.has_error()) {
    const auto message = output.error().message();
    output.clear_error();
    diagnostics.error("object output", message);
    return false;
  }
  return true;
}
auto EmitObject(const IR::Program &program, std::string_view path,
                Diagnostic::Diagnostics &diagnostics) -> bool {
  return EmitObjectFile(program, path, diagnostics, true);
}
} // namespace Chtholly::CodeGen
