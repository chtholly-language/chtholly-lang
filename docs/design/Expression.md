<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
## 值块

在Chtholly之中，`{}`是一个表达式，具有返回尾随表达式的能力。

```cns
let count = 10;
let value =  {
    count = count + 1;
    0  // 返回尾随表达式
};

{
    let value = value + 1;
}
```

## if表达式

if语句 可以作为表达式。

```cns
let result = if (value < 0) {
        -1
    } else if (value == 0) {
        0
    } else {
        1
    };
```

## loop表达式

loop语句 可以作为表达式。

```cns
let result = loop {
    break 1;
};
```