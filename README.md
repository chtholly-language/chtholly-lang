<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
# Chtholly

**Chtholly 是一门面向系统编程的静态类型、提前编译语言，围绕资源所有权、可检查的接口契约和可预测的程序行为进行设计。**

Chtholly 的目标是让开发者清楚表达值如何传递、资源由谁负责、接口需要什么条件，以及失败如何处理。编译器将这些规则转化为静态检查，并通过 LLVM 生成本机代码。

Chtholly 有两个长期目标
- **C / C++ 的安全层。** 与Carbon不同，Chtholly不追求与C++的完全互操作，而是追求足够的互操作。
- **通用系统编程语言。** 成为一门通用系统编程语言是Chtholly语言的长期演进目标。

## 语言方向

- **明确的值与资源语义。** 使用方式应能够反映内存模型，普通值，复制，移动，借用，独占具有相对明确的表面。
- **可检查的接口。** 类型、约束和公开契约共同描述调用要求、资源转移及行为边界，使使用者能够依据接口理解和检查代码。
- **可预测的数值与失败行为。** 已定型数值不隐式转换，`as` 以保值为原则；数值检查、普通错误和程序终止各有明确语义。
- **可组合的编译期能力。** 类型域、接口和受限元编程服务于类型关系与契约表达，保持明确的求值和验证边界。

## 当前进展

Chtholly 是一个实验性的编译器实现，当前处于非常早期的 **Testing-And-Governance（R1）**阶段：

当前支持的程序为：

```cns
fn main() -> i32 {
    return 0;
}
```

源文件使用 `.cns`。

## 开始使用

开发环境默认为 Windows x64、MSVC、Windows SDK、CMake、Ninja 和 LLVM **18.1.8**。编译器实现使用 C++20。

Chtholly 开发者可从 [Yhlight/llvm-package](https://github.com/Yhlight/llvm-package) 获取 LLVM 预编译开发包。

按照[构建并运行第一个程序](docs/guide/getting-started.md)准备环境、构建编译器并运行示例。当前编译器命令形式为：

```text
chthollyc examples/hello/main.cns -o out/manual/hello.exe
```

输出父目录须事先创建。编译器退出码和生成程序的退出码分别说明在[命令行参考](docs/reference/compiler.md)中。

## 文档与参与

| 想了解什么 | 从这里开始 |
| --- | --- |
| 如何构建和运行 | [入门指南](docs/guide/getting-started.md) |
| 编译器命令、诊断和退出码 | [命令行参考](docs/reference/compiler.md) |
| 编译器结构与模块边界 | [架构概览](docs/architecture/overview.md) |
| 开发计划、审查和文档规范 | [文档索引](docs/README.md) |
| 如何贡献 | [贡献说明](CONTRIBUTING.md) |
| 路线文档 | [路线文档](docs/roadmap/roadmap.md) |
| 测试说明 | [测试说明](testing/README.md) |

## 许可证

项目采用 **MIT OR Apache-2.0** 双许可证，可选择 [MIT](LICENSE-MIT) 或 [Apache License 2.0](LICENSE-APACHE)。
