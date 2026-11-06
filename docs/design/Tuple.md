<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
## 元组

你可以使用`(Type, Type, Type)`来创建一个元组类型，单元组请使用`(Type, )`。

```cns
let v: (i32, bool) = (10, true);
let first = v.0;
let second = v.1;
```

元组使用 .N 来访问具体某个成员。

元组类型如果需要获得Copy能力，需要成员全部有Copy的能力才可以Copy，否则默认为Move。

### 解构

元组支持解构操作。

```cns
let (a, b) = (1, 2);         // a、b 不可写
let (var c, d) = (3, 4);     // 只有 c 可写
var (e, f) = (5, 6);         // e、f 可写
var (var g, h) = (7, 8);     // 冗余 var 合法
let (first, .., last) = (1, 2, 3, 4);
```

### OR

对于元组，你可以使用 OR 模式的匹配，OR 模式即一分支，多次尝试，即尝试多种可能，若其中有一个满足要求，则进入该分支。

```cns
fn choose(_ flag: bool) -> i32 {
    let ((true, number) | (false, number)) = (flag, 42);
    return number;
}
```

详细的模式匹配说明，请阅读[Match](./Match.md)文档