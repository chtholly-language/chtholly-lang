<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
## 控制流
### if语句

if语句，其作用为根据条件是否成立来执行某一分支。

```cns
if (value < 0) {
    return -1;
} else if (value == 0) {
    return 0;
} else {
    return 1;
}
```

### while语句

while语句，其作用为根据条件判断并执行循环。

在循环体之中，可以使用 `break;` 终止循环，
也可以使用 `continue;` 跳过本次循环。

```cns
while (index < values.len) {
    sum = sum + values[index];
}
```

### loop语句

loop语句，即无限循环。

loop语句之中仍可以使用 `break` 和 `continue`，
但相对于while语句来说，loop支持`break value;`。

```cns
loop {

}
```

### 标签与目标

可以为每一个循环设置一个标签，以便让break跳出某一个具体的循环，而不仅仅是跳出当前循环。

```cns
outer: loop {
    loop {
        break @outer;
    }
}
```