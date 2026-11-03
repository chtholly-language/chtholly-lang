<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
# 编译器架构概览

## 当前 Bootstrap 数据流

```text
.cns → Source → Lex → Parse → Check → SemIR → CodeGen → COFF 目标文件
                                                             ↓
                                               clang-cl / lld-link → exe
```

Driver 组织整个流程、工具调用和产物发布；Diagnostic 提供错误定位及输出。图中的箭头表示数据处理顺序，不表示每对相邻模块都直接链接对方。

| 模块 | 当前职责 |
| --- | --- |
| Source | 读取源码、UTF-8 验证、源范围和定位 |
| Lex | Bootstrap token、空白与行注释 |
| Parse | 最小函数结构及源码范围 |
| Check | main 签名、返回字面量及范围检查 |
| SemIR | 仅承载已检查的 i32 返回常量 |
| CodeGen | LLVM IR、目标设置与 COFF 目标文件生成 |
| Driver | 命令行、阶段编排、工具定位、链接和最终发布 |
| Diagnostic | 编译过程中的诊断记录与呈现 |

各模块导出自己的 `include/Chtholly/<Module>`，C++ 根命名空间为 `Chtholly`，子命名空间按模块划分。依赖通过模块级 CMake target 明确表达。

LLVM IR、TargetMachine 等实现位于 CodeGen 私有边界内。Driver 私有使用 LLVM Support 的进程和文件系统能力；Source/Lex/Parse/Check/SemIR 的公共接口不暴露 LLVM 类型。可执行入口依赖 Driver。

## 依赖和生命周期原则

公共接口只暴露使用者需要的概念。实现细节留在 `.cpp`，前置声明在适用时减少 include；PImpl 只在确有隔离价值时引入。模块不随意包含其他模块的内部头文件。

拥有者、借用视图和失效时点应明确。失败不发布可供后续阶段消费的成功结果；源码检查、目标生成和输出发布各自说明能够保证什么。CMake 的 PRIVATE/PUBLIC/INTERFACE 依据实际接口传播需要选择。