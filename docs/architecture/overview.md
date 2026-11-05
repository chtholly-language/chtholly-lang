<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
# 编译器架构概览

## 当前 Bootstrap 数据流

```text
.cns → Source → Lex → Parse → Check → SemIR → Lower → IR 验证 → CodeGen → COFF 目标文件
                                                             ↓
                                               clang-cl / lld-link → exe
```

Driver 组织整个流程、工具调用和产物发布；Diagnostic 提供错误定位及输出。图中的箭头表示数据处理顺序，不表示每对相邻模块都直接链接对方。

| 模块 | 当前职责 |
| --- | --- |
| Target | Windows x64 的基础类型身份、位宽、对齐；不读取宿主 C 类型 | 只读值；CodeGen 对照 LLVM 数据布局，冲突即失败 |
| Source | 文件字节和字节位置；UTF-8 检查仍由 Lex 负责 | Buffer 拥有文本和行起点；读取失败不返回 Buffer |
| Diagnostic | 编译错误记录与渲染 | 记录拥有消息；只有渲染源码位置时借用仍有效的 Buffer |
| Lex | UTF-8、token 边界、数字形状、注释 | token 范围借用同一 Buffer；非法输入不返回完整流 |
| Parse | 函数和表达式结构、优先级、分号、类型语法 | AST 拥有节点数组，范围仍属于 Buffer；失败不返回占位树 |
| Numeric | 精确数学整数、定宽位模式和目标浮点；不决定名称和类型推导 | Value 私有拥有 LLVM 存储；成功、规定失败、预算和内部错误分别表达 |
| SemIR | 类型、声明、局部位置、规范操作、正常完成性 | Program 独立拥有记录和常量；成功后只读，不借用 AST |
| Check | 作用域、定型、可写性、恢复资格和常量检查 | source/tree 必须属于同一次输入；任一错误均不发布成功 Program |
| IR | 执行块、值、位置和独立验证 | ID 只在所属程序/函数记录中解释；失败不创建 IR::Program |
| Lower | 将受检语义变成明确求值顺序和分支 | 消费只读 SemIR；拥有输出 Draft，然后交给验证器 |
| CodeGen | 内部 ABI、LLVM 指令、COFF | 只收 IR::Program；写调用者给出的临时对象，失败由 Driver 清理 |
| Runtime | 受检数值辅助、浮点环境、语言终止 | Windows 目标代码；按字接口；不包括字符串库、分配器、用户展开 |
| Driver | 阶段生命周期、工具与运行库身份、可靠发布 | 只在链接成功并复查目的地后替换输出；失败保留已有成功产物 |

各模块导出自己的 `include/Chtholly/<Module>`，C++ 根命名空间为 `Chtholly`，子命名空间按模块划分。依赖通过模块级 CMake target 明确表达。

LLVM IR、TargetMachine 等实现位于 CodeGen 私有边界内。Driver 私有使用 LLVM Support 的进程和文件系统能力；Source/Lex/Parse/Check/SemIR 的公共接口不暴露 LLVM 类型。可执行入口依赖 Driver。

## 依赖和生命周期原则

公共接口只暴露使用者需要的概念。实现细节留在 `.cpp`，前置声明在适用时减少 include；PImpl 只在确有隔离价值时引入。模块不随意包含其他模块的内部头文件。

拥有者、借用视图和失效时点应明确。失败不发布可供后续阶段消费的成功结果；源码检查、目标生成和输出发布各自说明能够保证什么。CMake 的 PRIVATE/PUBLIC/INTERFACE 依据实际接口传播需要选择。
