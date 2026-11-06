<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
## match

match语句可以用于对输入进行结果匹配，match除了可以作为语句外，还可以作为表达式。

match语句必须要穷尽输入的所有可能结果，若输入存在所有可能都无法满足的情况下，需要使用`_`来表示没有匹配的结果。

```cns
enum Choice { Value { number: i32 }, Empty }

fn score(_ input: Choice) -> i32 {
    return match (input) {
        Choice::Value { number } => number,
        Choice::Empty => 0,
    };
}

fn main() -> i32 {
    return score(Choice::Value { number: 42 });
}
```

除了可以作用于枚举，也可以用于结构体的匹配。

```cns
struct Token { value: i32 }

fn positive(_ input: Token) -> bool {
    return match (input) {
        token if (token.value > 0) => true,
        _ => false,
    };
}
```

如你所见，上述对Token进行模式匹配时，用到了if，这里的if是match的一个能力，称为守卫，守卫用于判断匹配的结果是否符合条件，如果不符合，则进入下一个分支。

match还可以在一个分支之中尝试多次，你可以使用`|`来分隔两个匹配尝试。

```cns
enum Choice { A { x: i32, y: i32 }, B { x: i32, y: i32 } }

fn selected(_ input: Choice) -> i32 {
    return match (input) {
        Choice::A { x: value, y: _ } |
        Choice::B { x: _, y: value } => value,
    };
}
```

### 为匹配项的成员设置名称

就像上述所演示的一样，你可以为匹配的成员设置一个新的名称，也可以使用`_`表示不设置名称。

```cns
enum Choice { A { x: i32, y: i32 }, B { x: i32, y: i32 } }

match (input) {
    Choice::A { x: value, y: _ } => value,
    Choice::B { x: value, y: _ } => 2
}
```

### 忽略匹配项的成员

除了上述外，还可以使用`..`来忽略匹配的成员。

```cns
enum Choice { A { x: i32, y: i32 }, B { x: i32, y: i32 } }

match (input) {
    Choice::A { x: value, .. } => value,
    Choice::B { .. } => 1
}
```

### var

在match之中，通常是以不可变的方式来绑定匹配成员，如果需要以可变的形式，需要使用var。

```cns
match (input) {
    var token => 1,
}
```

```cns
enum E {
    Pair { x: i32 },
    Empty,
}

fn extract(_ input: E) -> i32 {
    return match (input) {
        E::Pair { x: var v } => {  // 也可以简写为var x
            v = v + 1;
            v
        },
        E::Empty => 0,
    };
}
```