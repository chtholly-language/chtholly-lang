<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
# 构建并运行第一个程序

## 1. 准备工具

安装带 C++ 桌面开发组件的 Visual Studio / MSVC x64、Windows SDK、CMake 3.25+ 和 Ninja。准备 LLVM 18.1.8 完整开发包，包含头文件、库、`LLVMConfig.cmake`、X86 后端、`clang-cl.exe` 和 `lld-link.exe`。

优先获取 [Chtholly 开发用 LLVM 预编译包](https://github.com/Yhlight/llvm-package/releases/tag/v18.1.8)中的 X86/AArch64 RelWithDebInfo 版本。

## 2. 导入 MSVC 并选择 LLVM

在同一个 PowerShell 进程中操作。在实际操作中，请替换下述路径为具体的自机路径。

```powershell
Import-Module "D:\yhprogram\visual studio\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "D:\yhprogram\visual studio" -Arch amd64 -SkipAutomaticLocation

# 替换为解压后实际包含 bin、include、lib 的根目录。
$chthollyLlvmRoot = "D:\deps\llvm-18.1.8"
$env:CHTHOLLY_LLVM_DIR = Join-Path $chthollyLlvmRoot "lib\cmake\llvm"
$env:CHTHOLLY_LLVM_BIN = Join-Path $chthollyLlvmRoot "bin"
```

先确认 `lib\cmake\llvm\LLVMConfig.cmake` 和 `bin` 中所需工具确实位于所选根目录；压缩包可能包含额外目录层级。不要把本机路径写入共享 Preset。

## 3. 构建编译器

```powershell
cmake --preset bootstrap-debug
if ($LASTEXITCODE -ne 0) { throw "Configure failed" }

cmake --build --preset bootstrap-debug --parallel 3
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
```

输出编译器为 `out\build\bootstrap-debug\bin\chthollyc.exe`。Release 使用 `bootstrap-release`，并位于对应目录。构建并发可按机器资源调整。

配置会检查所选 LLVM 安装及其 `clang-cl`/`lld-link` 版本，缺少工具或版本不匹配会失败。配置和构建不会自动下载或升级依赖。LLVM 包自身的 RelWithDebInfo 与 Chtholly 的 Debug/Release 是不同配置；能否组合取决于工具链说明中的 ABI 条件。

需要干净构建时可使用新的目录，保留原有输出：

```powershell
cmake --preset bootstrap-debug -B out/build/bootstrap-debug-fresh
if ($LASTEXITCODE -ne 0) { throw "Fresh configure failed" }

cmake --build out/build/bootstrap-debug-fresh --parallel 3
if ($LASTEXITCODE -ne 0) { throw "Fresh build failed" }
```

使用这个新目录时，下文的编译器路径也相应替换。

## 4. 编译并运行示例

仓库的[最小示例](../../examples/hello/main.cns)使用以下程序：

```cns
fn main() -> i32 {
    return 0;
}
```

在刚才的 PowerShell 中：

```powershell
New-Item -ItemType Directory -Force .\out\manual | Out-Null

& .\out\build\bootstrap-debug\bin\chthollyc.exe `
    .\examples\hello\main.cns -o .\out\manual\hello.exe
$compileExit = $LASTEXITCODE
if ($compileExit -ne 0) { throw "Compilation failed: $compileExit" }

& .\out\manual\hello.exe
$programExit = $LASTEXITCODE
if ($programExit -ne 0) { throw "Unexpected program exit: $programExit" }
```

预期编译器退出码为 0，生成程序也返回 0，且没有输出。

编译失败时不运行输出目录中的旧程序。更多规则见[命令行参考](../reference/compiler.md)。