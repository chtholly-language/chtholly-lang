## R0 Bootstrap

**目标**：
- 实现完整的最小编译链：读取 .cns、词法/语法/语义检查、LLVM 目标文件生成，以及原生链接

**完成情况**: 已完成

## R1 Testing And Governance

**目标**：
- Testing Infrastructure 与 Engineering Governance
    > 在Bootstrap之上建立验证设施和工程规则，包括测试组织、模块依赖约束、变更检查及维护规范。

**完成情况**: 已完成

## R2 Primitive Types And Function

**目标**：
- 支持基本数据类型，字面量，及相关运算规则
- 支持变量创建，赋值失败恢复
- 支持函数，函数调用，if语句与常见运算符
- 支持保守的类型转换

详细请见docs/design

**完成情况**: 已完成

## R3 Aggregates, Patterns, And Ownership Flow

**目标**：
- 支持结构体、枚举、元组、固定数组、类型别名和标量常量。
- 实现受检 Copy、显式移动、部分初始化及恢复赋值。
- 支持解构、模式守卫、或模式及穷尽性检查。
- 支持循环、带值 break 和标签控制流。
- 引入共享类型身份及受验证的初始化分析。
- 实现 ABI v3 聚合参数传递与数组边界检查。
- 支持位置和标签实参混用，并保持源码求值顺序。

详细请见docs/design

**完成情况**: 已完成