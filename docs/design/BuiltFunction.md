<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
## 内置函数

| 函数签名 | 行为 |
| --- | --- |
| `panic() -> !` | 输出 `Panic` 类别及源码位置，以 120 退出 |
| `abort() -> !` | 不输出语言诊断，以 121 退出 |
| `assert(_ condition: bool) -> ()` | 条件为 true 时正常返回；为 false 时输出 `Assertion` 类别及位置，以 120 退出 |
