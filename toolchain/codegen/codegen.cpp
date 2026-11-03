// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/CodeGen/codegen.h>
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/SemIR/program.h>
#include <Chtholly/build_config.h>

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallingConv.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace Chtholly::CodeGen {

auto EmitObject(const SemIR::Program &program, std::string_view objectPath,
                Diagnostic::Diagnostics &diagnostics) -> bool {
  static const bool initialized = [] {
    return !llvm::InitializeNativeTarget() && !llvm::InitializeNativeTargetAsmPrinter();
  }();
  if (!initialized) {
    diagnostics.error("code generation", "cannot initialize the LLVM X86 target");
    return false;
  }
  std::string error;
  const auto *target = llvm::TargetRegistry::lookupTarget(BuildConfig::Target, error);
  if (!target) {
    diagnostics.error("code generation", error);
    return false;
  }
  llvm::TargetOptions options;
  std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
      BuildConfig::Target, "generic", "", options, std::nullopt,
      std::nullopt, llvm::CodeGenOptLevel::None));
  if (!machine) {
    diagnostics.error("code generation", "cannot create the Windows x64 target machine");
    return false;
  }

  // Declared in dependency order: module and builder die before their context.
  llvm::LLVMContext context;
  llvm::Module module("chtholly-bootstrap", context);
  module.setTargetTriple(BuildConfig::Target);
  module.setDataLayout(machine->createDataLayout());
  auto *type = llvm::FunctionType::get(llvm::Type::getInt32Ty(context), false);
  auto *main = llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage,
                                     "main", module);
  main->setCallingConv(llvm::CallingConv::C);
  auto *entry = llvm::BasicBlock::Create(context, "entry", main);
  llvm::IRBuilder<> builder(entry);
  builder.CreateRet(llvm::ConstantInt::get(
      llvm::Type::getInt32Ty(context), static_cast<std::uint64_t>(program.returnValue())));

  std::string verification;
  llvm::raw_string_ostream verificationStream(verification);
  if (llvm::verifyModule(module, &verificationStream)) {
    diagnostics.error("LLVM IR verification", verification);
    return false;
  }

  std::error_code fileError;
  llvm::raw_fd_ostream output(
      llvm::StringRef(objectPath.data(), objectPath.size()), fileError,
      llvm::sys::fs::OF_None);
  if (fileError) {
    diagnostics.error("object output", std::string(objectPath) + ": " + fileError.message());
    return false;
  }
  llvm::legacy::PassManager passes;
  if (machine->addPassesToEmitFile(passes, output, nullptr,
                                  llvm::CodeGenFileType::ObjectFile)) {
    diagnostics.error("code generation", "LLVM cannot emit a COFF object for the selected target");
    return false;
  }
  passes.run(module);
  output.close();
  if (output.has_error()) {
    const auto message = output.error().message();
    output.clear_error();
    diagnostics.error("object output", std::string(objectPath) + ": " + message);
    return false;
  }
  return true;
}

} // namespace Chtholly::CodeGen
