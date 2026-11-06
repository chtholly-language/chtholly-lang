<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
## 类型别名

你可以使用 `alias Name = Type;` 来创建一个类型的别名。

当前 `alias` 仅限于全局中定义，暂时不支持局部定义。

```cns
alias Count = usize;
alias Pair = (i32, bool);
alias Row = i32[3];

struct Point { x: i32, y: i32 }
alias Position = Point;

fn main() -> i32 {
    let count: Count = 3usize;
    let pair: Pair = (42, true);
    let row: Row = [10, 11, 21];
    assert(count == row.len);

    let a: Position = Point { x: 20, y: 22 };
    let b = Position { x: 1, y: 2 };

    return pair.0;
}
```

类型别名允许前向引用，但是不允许循环。

```cns
alias Count = Small;
alias Small = u8;  // 可以前向引用
```

值得一提，别名就是此类型的指代，并不会影响这个类型本身具有什么能力。