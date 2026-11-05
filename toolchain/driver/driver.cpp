// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Driver/driver.h>

#include <Chtholly/Check/check.h>
#include <Chtholly/CodeGen/codegen.h>
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/Lex/lex.h>
#include <Chtholly/Lower/lower.h>
#include <Chtholly/Parse/parse.h>
#include <Chtholly/Runtime/abi.h>
#include <Chtholly/Source/source.h>
#include <Chtholly/Target/target.h>
#include <Chtholly/build_config.h>

#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/SHA256.h>

#include <windows.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Chtholly::Driver {
namespace {
namespace FS = std::filesystem;

constexpr std::string_view Usage =
    "Usage: chthollyc <input.cns> -o <output.exe>\n"
    "       chthollyc --help\n"
    "       chthollyc --version\n";

auto NativePath(std::string_view utf8) -> FS::path {
  std::u8string value;
  value.reserve(utf8.size());
  for (unsigned char c : utf8) {
    value.push_back(static_cast<char8_t>(c));
  }
  return FS::path(value);
}

auto UTF8Path(const FS::path &path) -> std::string {
  const auto value = path.u8string();
  return {value.begin(), value.end()};
}

auto FindRuntime(Diagnostic::Diagnostics &diagnostics)
    -> std::optional<FS::path> {
  std::vector<wchar_t> module(32768);
  const auto length = GetModuleFileNameW(nullptr, module.data(),
                                         static_cast<DWORD>(module.size()));
  if (length == 0 || length >= module.size()) {
    diagnostics.error("runtime", "cannot locate compiler executable");
    return std::nullopt;
  }
  const auto directory =
      FS::path(std::wstring(module.data(), length)).parent_path() / L"lib";
  const auto archive = directory / L"chtholly-runtime.lib";
  std::ifstream metadata(directory / L"chtholly-runtime.meta");
  std::array<std::string, 6> records;
  for (auto &record : records) {
    if (!std::getline(metadata, record)) {
      diagnostics.error("runtime", "missing or incomplete runtime metadata: " +
                                       UTF8Path(directory));
      return std::nullopt;
    }
    if (!record.empty() && record.back() == '\r') {
      record.pop_back();
    }
  }
  std::string extra;
  if (std::getline(metadata, extra) ||
      records[0] != "abi=" + std::to_string(Runtime::ABIVersion) ||
      records[1] != "target=" + std::string(Target::WindowsX64.Triple) ||
      records[2] != "llvm=18.1.8" ||
      records[3] != "source=3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff" ||
      records[5] !=
          "implementation=" + std::string(BuildConfig::RuntimeImplementation)) {
    diagnostics.error("runtime",
                      "runtime identity does not match this compiler");
    return std::nullopt;
  }
  std::ifstream input(archive, std::ios::binary);
  llvm::SHA256 hash;
  std::array<char, 65536> bytes;
  std::size_t total = 0;
  while (input) {
    input.read(bytes.data(), bytes.size());
    const auto count = input.gcount();
    if (count > 0) {
      hash.update(
          llvm::StringRef(bytes.data(), static_cast<std::size_t>(count)));
      total += static_cast<std::size_t>(count);
    }
  }
  if (total == 0 || !input.eof()) {
    diagnostics.error("runtime", "cannot read a nonempty runtime archive: " +
                                     UTF8Path(archive));
    return std::nullopt;
  }
  constexpr char Hex[] = "0123456789abcdef";
  std::string digest;
  for (const auto byte : hash.final()) {
    digest += Hex[byte >> 4];
    digest += Hex[byte & 15];
  }
  if (records[4] != "sha256=" + digest) {
    diagnostics.error("runtime", "runtime archive checksum mismatch");
    return std::nullopt;
  }
  return archive;
}

struct Options {
  std::string Input;
  std::string Output;
};

auto ParseOptions(std::span<const std::string> arguments)
    -> std::optional<Options> {
  Options options;
  bool positionalOnly = false;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const auto &argument = arguments[i];
    if (!positionalOnly && argument == "--") {
      positionalOnly = true;
    } else if (!positionalOnly && argument == "-o") {
      if (!options.Output.empty() || i + 1 == arguments.size() ||
          arguments[i + 1].empty() || arguments[i + 1].starts_with('-')) {
        return std::nullopt;
      }
      options.Output = arguments[++i];
    } else if (argument.empty() ||
               (!positionalOnly && argument.starts_with('-')) ||
               !options.Input.empty()) {
      return std::nullopt;
    } else {
      options.Input = argument;
    }
  }
  if (options.Input.empty() || options.Output.empty()) {
    return std::nullopt;
  }
  return options;
}

// This invocation owns a unique directory beside the output, on the same
// volume. Cleanup only removes its known files, never a caller's directory
// tree.
class TemporaryFiles {
public:
  TemporaryFiles() = default;
  TemporaryFiles(const TemporaryFiles &) = delete;
  auto operator=(const TemporaryFiles &) -> TemporaryFiles & = delete;
  ~TemporaryFiles() {
    if (Directory.empty()) {
      return;
    }
    std::error_code ignored;
    for (const auto *name :
         {"main.obj", "linked.exe", "stdout.txt", "stderr.txt"}) {
      FS::remove(Directory / name, ignored);
    }
    FS::remove(Directory, ignored);
  }

  auto create(const FS::path &parent,
              Diagnostic::Diagnostics &diagnostics) -> bool {
    llvm::SmallString<256> directory;
    const auto prefix = UTF8Path(parent / ".chtholly");
    if (const auto error =
            llvm::sys::fs::createUniqueDirectory(prefix, directory)) {
      diagnostics.error("temporary output", prefix + ": " + error.message());
      return false;
    }
    Directory = NativePath(directory.str().str());
    return true;
  }

  auto file(const char *name) const -> FS::path { return Directory / name; }

private:
  FS::path Directory;
};

auto ReadLog(const FS::path &path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return "[cannot read tool log: " + UTF8Path(path) + "]\n";
  }
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

auto Execute(const FS::path &tool, const std::vector<std::string> &storage,
             const TemporaryFiles &temporary, std::string_view stage,
             Diagnostic::Diagnostics &diagnostics)
    -> std::optional<std::string> {
  const auto program = UTF8Path(tool);
  std::vector<llvm::StringRef> arguments;
  arguments.reserve(storage.size());
  for (const auto &argument : storage) {
    arguments.emplace_back(argument);
  }
  const auto stdoutPath = UTF8Path(temporary.file("stdout.txt"));
  const auto stderrPath = UTF8Path(temporary.file("stderr.txt"));
  const std::array<std::optional<llvm::StringRef>, 3> redirects = {
      llvm::StringRef(""), llvm::StringRef(stdoutPath),
      llvm::StringRef(stderrPath)};
  std::string error;
  bool failed = false;
  const int status = llvm::sys::ExecuteAndWait(
      program, arguments, std::nullopt, redirects, 120, 0, &error, &failed);
  auto log = ReadLog(temporary.file("stdout.txt")) +
             ReadLog(temporary.file("stderr.txt"));
  if (failed || status != 0) {
    diagnostics.error(std::string(stage),
                      "tool '" + program + "' returned " +
                          std::to_string(status) +
                          (error.empty() ? "" : ": " + error) + "\n" + log);
    return std::nullopt;
  }
  return log;
}

auto VersionMatches(std::string_view output, std::string_view marker) -> bool {
  const auto position = output.find(marker);
  if (position == std::string_view::npos) {
    return false;
  }
  const auto version = output.substr(position + marker.size());
  constexpr std::string_view Expected = BuildConfig::LLVMVersion;
  return version.starts_with(Expected) &&
         (version.size() == Expected.size() ||
          std::string_view(" \t\r\n").find(version[Expected.size()]) !=
              std::string_view::npos);
}

struct Tools {
  FS::path Clang;
  FS::path Linker;
};

auto FindTools(const TemporaryFiles &temporary,
               Diagnostic::Diagnostics &diagnostics) -> std::optional<Tools> {
  // The wide environment API avoids interpreting a Unicode path in the ANSI
  // code page. An explicitly empty or invalid override must not fall back.
  const auto *overrideDirectory = _wgetenv(L"CHTHOLLY_LLVM_BIN");
  if (overrideDirectory && *overrideDirectory == L'\0') {
    diagnostics.error("toolchain", "CHTHOLLY_LLVM_BIN is explicitly empty");
    return std::nullopt;
  }
  auto directory = overrideDirectory ? FS::path(overrideDirectory)
                                     : NativePath(BuildConfig::LLVMTools);
  std::error_code error;
  directory = FS::absolute(directory, error);
  if (error) {
    diagnostics.error("toolchain",
                      "cannot resolve LLVM tool directory: " + error.message());
    return std::nullopt;
  }
  Tools tools{directory / "clang-cl.exe", directory / "lld-link.exe"};
  for (const auto &tool : {tools.Clang, tools.Linker}) {
    if (!FS::is_regular_file(tool, error)) {
      diagnostics.error("toolchain", "missing tool '" + UTF8Path(tool) + "'" +
                                         (error ? ": " + error.message() : ""));
      return std::nullopt;
    }
  }
  for (const auto &tool : {tools.Clang, tools.Linker}) {
    const auto log = Execute(tool, {UTF8Path(tool), "--version"}, temporary,
                             "tool version", diagnostics);
    if (!log) {
      return std::nullopt;
    }
    const auto marker = tool == tools.Clang ? "clang version " : "LLD ";
    if (!VersionMatches(*log, marker)) {
      diagnostics.error("toolchain",
                        "tool '" + UTF8Path(tool) + "' must match LLVM " +
                            BuildConfig::LLVMVersion + "\n" + *log);
      return std::nullopt;
    }
  }
  return tools;
}

auto CheckDestination(const FS::path &input, const FS::path &output,
                      Diagnostic::Diagnostics &diagnostics) -> bool {
  std::error_code error;
  if (!FS::is_directory(output.parent_path(), error)) {
    diagnostics.error("output",
                      "output parent must be an existing directory: '" +
                          UTF8Path(output.parent_path()) + "'" +
                          (error ? ": " + error.message() : ""));
    return false;
  }
  const auto status = FS::symlink_status(output, error);
  if (error && error != std::errc::no_such_file_or_directory) {
    diagnostics.error("output", UTF8Path(output) + ": " + error.message());
    return false;
  }
  if (!FS::exists(status)) {
    return true;
  }
  if (!FS::is_regular_file(status)) {
    diagnostics.error("output",
                      "destination must be a regular file, not a directory or "
                      "symbolic link: '" +
                          UTF8Path(output) + "'");
    return false;
  }
  const bool same = FS::equivalent(input, output, error);
  if (error || same) {
    diagnostics.error("output", same ? "input and output refer to the same file"
                                     : "cannot compare input and output: " +
                                           error.message());
    return false;
  }
  return true;
}

auto Compile(const Options &options) -> int {
  Diagnostic::Diagnostics diagnostics;
  std::optional<Source::Buffer> source;
  const auto failure = [&] {
    diagnostics.render(std::cerr, source ? &*source : nullptr);
    return 1;
  };
  std::error_code error;
  const auto input = FS::absolute(NativePath(options.Input), error);
  if (error) {
    diagnostics.error("input", "cannot resolve '" + options.Input +
                                   "': " + error.message());
    return failure();
  }
  std::string readError;
  source = Source::Buffer::Read(input, readError);
  if (!source) {
    diagnostics.error("input",
                      "cannot read '" + options.Input + "': " + readError);
    return failure();
  }
  const auto tokens = Lex::Lex(*source, diagnostics);
  if (!tokens) {
    return failure();
  }
  const auto tree = Parse::Parse(*tokens, diagnostics);
  if (!tree) {
    return failure();
  }
  const auto program = Check::Check(*source, *tree, diagnostics);
  if (!program) {
    return failure();
  }
  const auto execution = Lower::Lower(*program, diagnostics);
  if (!execution) {
    return failure();
  }
  const auto runtime = FindRuntime(diagnostics);
  if (!runtime) {
    return failure();
  }
  const auto output = FS::absolute(NativePath(options.Output), error);
  if (error) {
    diagnostics.error("output", "cannot resolve '" + options.Output +
                                    "': " + error.message());
    return failure();
  }
  if (!CheckDestination(input, output, diagnostics)) {
    return failure();
  }
  TemporaryFiles temporary;
  if (!temporary.create(output.parent_path(), diagnostics)) {
    return failure();
  }
  const auto tools = FindTools(temporary, diagnostics);
  if (!tools) {
    return failure();
  }
  const auto object = UTF8Path(temporary.file("main.obj"));
  if (!CodeGen::EmitObject(*execution, object, diagnostics)) {
    return failure();
  }
  const auto executable = temporary.file("linked.exe");
  // LLVM 18's MSVC toolchain does not consume the generic --ld-path option.
  // Forward -B through clang-cl to prioritize this exact, prechecked directory.
  const std::vector<std::string> arguments = {
      UTF8Path(tools->Clang),
      "/nologo",
      "--target=" + std::string(Target::WindowsX64.Triple),
      "-fuse-ld=lld",
      "/clang:-B" + UTF8Path(tools->Linker.parent_path()),
      object,
      UTF8Path(*runtime),
      "/Fe" + UTF8Path(executable),
      "/link",
      "/subsystem:console",
      "/machine:x64",
      "/incremental:no",
      "/defaultlib:msvcrt",
      "/defaultlib:oldnames",
  };
  if (!Execute(tools->Clang, arguments, temporary, "link", diagnostics)) {
    return failure();
  }
  if (!FS::is_regular_file(executable, error)) {
    diagnostics.error("link", "linker reported success without producing '" +
                                  UTF8Path(executable) + "'");
    return failure();
  }
  // Recheck before publication. The final path has not been modified by this
  // invocation. No copy/delete fallback may destroy an existing successful exe.
  if (!CheckDestination(input, output, diagnostics)) {
    return failure();
  }
  if (!MoveFileExW(executable.c_str(), output.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    const auto code = static_cast<int>(GetLastError());
    diagnostics.error("publish", "cannot replace '" + UTF8Path(output) + "': " +
                                     std::system_category().message(code));
    return failure();
  }
  return 0;
}

} // namespace

auto Run(std::span<const std::string> arguments) -> int {
  if (arguments.size() == 1 && arguments.front() == "--help") {
    std::cout << Usage;
    return 0;
  }
  if (arguments.size() == 1 && arguments.front() == "--version") {
    std::cout << "Chtholly " << BuildConfig::Version << "\nLLVM "
              << BuildConfig::LLVMVersion << "\nTarget "
              << Target::WindowsX64.Triple << '\n';
    return 0;
  }
  const auto options = ParseOptions(arguments);
  if (!options) {
    std::cerr
        << "chthollyc: command line: error: expected one input and one -o "
           "output; unknown or duplicate option\n"
        << Usage;
    return 2;
  }
  return Compile(*options);
}

} // namespace Chtholly::Driver
