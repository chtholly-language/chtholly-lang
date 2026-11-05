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