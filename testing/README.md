<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
# 运行和编写测试

## 第一次运行

按[入门指南](../guide/getting-started.md)准备 MSVC x64 开发环境和 LLVM 18.1.8。

测试另外需要 Python 3.11 和 Google Test。Python 用来执行测试脚本；Google Test 用来编写和运行 C++ 模块测试。下文的 `python` 命令应当指向 Python 3.11。为与 CI 环境一致，建议使用 CMake 3.31.12、Ninja 1.13.2 和 Python 3.11.9。

先下载项目选定的 Google Test 源码。下面使用 `D:\deps\googletest`，可以换成其他尚未使用的目录：

```powershell
git clone --no-checkout https://github.com/google/googletest.git D:\deps\googletest
if ($LASTEXITCODE -ne 0) { throw 'Google Test 下载失败' }

git -C D:\deps\googletest checkout --detach b514bdc898e2951020cbdca1304b75f5950d1f59
if ($LASTEXITCODE -ne 0) { throw '无法选择指定的 Google Test 版本' }

$env:CHTHOLLY_GTEST_SOURCE_DIR = 'D:\deps\googletest'
```

下载只需要做一次，但新打开的 PowerShell 窗口后需要重新设置环境变量。

接着配置并构建项目：

```powershell
python scripts/prepare-build.py --build-dir out/build/dev-debug
if ($LASTEXITCODE -ne 0) { throw '构建目录准备失败' }

cmake --preset dev-debug
if ($LASTEXITCODE -ne 0) { throw '项目配置失败' }

cmake --build --preset dev-debug --parallel 3
if ($LASTEXITCODE -ne 0) { throw '项目构建失败' }
```

`dev-debug` 是仓库中已经定义好的配置名称：使用 Debug 模式，并启用测试。构建文件放在 `out/build/dev-debug`，编译器和 C++ 测试程序放在其中的 `bin` 目录。

第一条命令让 CMake 在配置时记录项目结构，供架构检查使用；第二条生成构建规则；第三条按规则编译。

最后运行测试：

```powershell
python scripts/run-ctest.py --build-dir out/build/dev-debug --report-dir out/reports --configuration dev-debug
if ($LASTEXITCODE -ne 0) { throw '测试或测试准备过程失败，请查看报告' }
```

这个入口会先核对测试列表，再调用 CTest 执行测试，最后保存报告。没有通过 `--label` 选择范围时，它执行当前构建配置下的全部测试。

## 修改代码后怎么运行

继续使用相同开发环境和构建目录时，修改已有 C++ 文件后，重新构建并运行测试即可：

```powershell
cmake --build --preset dev-debug --parallel 3
if ($LASTEXITCODE -ne 0) { throw '项目构建失败' }

python scripts/run-ctest.py --build-dir out/build/dev-debug --report-dir out/reports --configuration dev-debug
if ($LASTEXITCODE -ne 0) { throw '测试或测试准备过程失败，请查看报告' }
```

如果修改了 CMake 配置或新增测试登记，请重新执行 `cmake --preset dev-debug`。

只想检查 C++ 模块的单元测试时，在测试命令中加上 `--label unit`：

```powershell
python scripts/run-ctest.py --build-dir out/build/dev-debug --report-dir out/reports --configuration dev-debug --label unit
if ($LASTEXITCODE -ne 0) { throw '单元测试或测试准备过程失败，请查看报告' }
```

这样可以缩小日常排查范围。

## 测试失败后看哪里

运行结束时，脚本会打印以 `Report:` 开头的报告目录。

先打开该目录中的 `run.json`。`status` 说明本次运行的结果，`error` 在出现准备或登记错误时记录原因。如果没有进入测试执行阶段，报告会标明。

如果已经执行测试，请查看 `ctest.log`，找到失败的案例名称和输出。`junit.xml` 保存供 CI 等工具读取的逐案例结果。对于 `.cns` 源码案例，还可以在报告的 `cases` 子目录中查看编译和运行阶段分别保存的输出。

例如，检查返回值 42 的案例需要满足：

1. 编译器返回 0，表示编译成功。
2. 本次编译产生非空的可执行文件。
3. 执行这个文件，程序返回 42。

程序返回 42 是这里的预期结果。确认这些条件都成立后，测试脚本才返回 0，表示案例通过。编译器、生成程序和测试脚本的退出码各有用途。

如果报告说缺少案例或标签不符，应先检查测试登记。如果案例执行后失败，应对照源码输入、预期结果和实际输出查原因。预期必须有语言规则或独立计算作为依据，不能只为消除失败而修改。

## 增加一个源码案例

假设需要检查返回 7 的程序。在 `testing/e2e/exit_codes` 中添加 `return_7.cns`：

```cns
fn main() -> i32 {
    return 7;
}
```

在同一目录添加 `return_7.json`，说明怎样检查它：

```json
{
  "schema_version": 1,
  "name": "e2e.exit_codes.return_7",
  "kind": "run",
  "source": "return_7.cns",
  "basis": "main 返回的 i32 常量应成为生成程序的退出码",
  "compile": { "exit_code": 0 },
  "run": { "exit_code": 7 }
}
```

这里的 `source` 是相对于 JSON 文件的源码路径；`compile` 和 `run` 分别描述编译器和生成程序的预期结果；`basis` 解释这个预期来自什么规则。

然后在 `testing/e2e/CMakeLists.txt` 中增加一行：

```cmake
chtholly_case(exit_codes/return_7.json)
```

接着重新配置、构建并运行测试。

其他源码案例使用相同方式组织。`kind` 决定执行哪些步骤：

| `kind` | 检查内容 |
| --- | --- |
| `compile` | 编译器成功，并产生可执行文件；不运行生成程序。 |
| `compile_fail` | 编译器按预期拒绝源码，同时检查诊断位置和原因。 |
| `run` | 编译成功后运行生成程序，检查它的结果。 |

编写拒绝案例时，可以参考[缺分号案例](../../testing/regression/syntax/missing_semicolon.json)。它既要求编译失败，也要求错误发生在指定位置，并说明缺少分号。编译器崩溃、超时或缺少工具都不能算正确拒绝。

## 增加一个 C++ 模块测试

如果要直接检查 Lexer、Parser 等模块的接口，在 `testing/unit` 对应的 C++ 文件中编写 Google Test 案例。例如，[lex.cpp](../../testing/unit/lex.cpp) 中的 `TEST(lex, token_spans)` 检查 token 种类和源码位置，不需要先生成一个完整程序。

例如，新增案例写作 `TEST(lex, keyword_boundary)`，就要在 `testing/CMakeLists.txt` 中 `chtholly_lex_tests` 的 `CASES` 列表里登记 `lex.keyword_boundary`。运行时它会显示为 `unit.lex.keyword_boundary`。

显式登记让测试入口能够发现意外遗漏的案例。构建目录里的 `expected-tests.txt` 由 CMake 生成，不要手工修改它。登记完成后，重新配置、构建，再运行测试。
