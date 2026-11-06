<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
## let 与 var

在Chtholly之中，你可以使用let创建一个不可写的位置，使用var创建一个可写的位置，

变量声明完成之后，类型不会再发生改变。

```cns
let original = 40;      // 默认 i32
var answer = original;
answer = answer + 2;    // var创建可写的位置，合法
// original = 0;        // 拒绝：位置不可写
// answer = 2u8;        // 拒绝：类型已经确定为 i32
```

### 遮蔽

Chtholly不支持同作用域中存在两个名称一致的变量。

```cns
let v = 10;
let v = 20;  // 拒绝：同作用域中不能存在两个名称一致的变量

{
    let v = v;  // 左侧为内层作用域创建的v，右侧为外层作用域创建的v，合法
}
```

### 类型注解

可以在变量的名称后面使用`: Type`来声明这个变量预期的类型

```cns
let x: i32 = 10;
```

### 丢弃结果的即时运算

可以使用 `_` 来建立一个不存在名字的绑定，其运算后会即时被丢弃。

```cns
let _ = count + 1;
```

### 恢复分支

当变量初始化时，预期可能发生可恢复的错误时，可以预留恢复分支进行替换或反馈

```cns
fn example() -> i32 {
    let value = 127i8 + 1i8 else { return 7; };
    return value as i32;
}
```

详细的类型说明，请阅读[Type](./Type.md)文档
有关字面量方面，请阅读[Literal](./Literal.md)文档
有关类型转换方面，请阅读[As](./As.md)文档