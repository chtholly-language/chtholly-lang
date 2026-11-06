<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
## 结构体

你可以使用`struct`创建一个结构。

目前结构体的成员是模块私有，即当前文件可访问，具体跨模块访问控制未实现。

```cns
struct Point { 
    x: i32, 
    y: i32
}

fn main() -> i32 {
    let point = Point { y: 20, x: 22 };
    return pair.0;
}
```

### Copy 与 Move

结构体默认move，是否copy由是否实现了core::Copy接口决定。

```cns
struct Token { value: i32 }
struct Pair { first: Token, second: Token }

fn main() -> i32 {
    var pair = Pair {
        first: Token { value: 1 },
        second: Token { value: 2 },
    };

    let first = pair.first;
    let second_number = pair.second.value;

    // let whole = pair; // 拒绝：first 已移出，pair 不完整

    pair.first = Token { value: 40 };
    let whole = pair;

    return whole.first.value + second_number;
}
```

Token 没有 Copy，所以取得 `pair.first` 的值会移动它，`pair.second` 仍然有效，
读取其中的 i32 字段只是复制数字，不会移动整个 Token。

恢复赋值需要可写位置，把上例的 `var pair` 改为 `let pair`，移出仍可合法，恢复赋值则拒绝。

要想结构体支持Copy，需要使用`impl core::Copy for Type`，接入Copy接口。

```cns
struct Point { x: i32, y: i32 }
impl core::Copy for Point {}
```

现阶段暂不支持用户实现clone函数，目前的实现是由编译器检查结构体的成员是否均满足Copy的条件，
若满足，且实现了Copy接口时，则获得Copy的能力

### 解构

你可以对结构体进行结构操作。

```cns
struct Point { x: i32, y: i32 }

fn main() -> i32 {
    let point = Point { x: 20, y: 22 };
    let Point { x: horizontal, y } = point;  // 解构，如果不需要重命名，直接写一致的成员的名称即可
    return horizontal + vertical;
}
```

#### 忽略

你可以使用`..`忽略掉不需要的部分。

```cns
let Point { x: horizontal, .. } = point;
```

#### var

解构操作的成员默认是不可变的，如果需要可变，需要使用var。

```cns
let Point { var x: horizontal, var y } = point;
```

#### OR

对于结构体，你一样使用 OR 模式的匹配。

```cns
struct Data {
    flag: bool,
    left: i32,
    right: i32,
}

fn choose(_ input: Data) -> i32 {
    let (
        Data { flag: true, left: number, .. }
        |
        Data { flag: false, right: number, .. }
    ) = input;

    return number;
}
```

详细的模式匹配说明，请阅读[Match](./Match.md)文档