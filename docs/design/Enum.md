<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
## 枚举

你可以使用 `enum` 创建一个枚举。

枚举现在支持两种成员，`Name { name: Type }` 和 `Name`。

```cns
enum Choice { 
    Position { point: Point }, 
    Empty 
}

let point = Point { y: 20, x: 22 };
let choice = Choice::Position { point: Point { x: 20, y: 20 } };
let choice2 =  Choice::Position { point };  // 简写
```

### 模式匹配

你可以使用 `match` 语句来对枚举进行模式匹配。

```cns
enum Choice { 
    Value { 
        number: i8 
    }, 
    Empty
}

fn classify(_ input: Choice) -> i32 {
    return match (input) {
        Choice::Value { number: -128i8 } => 1,
        Choice::Value { number } if (number > 0i8) => 2,
        Choice::Value { number: _ } | Choice::Empty => 0,
    };
}
```