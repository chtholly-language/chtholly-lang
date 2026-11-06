<!-- SPDX-License-Identifier: MIT OR Apache-2.0 -->
## 数组

### 固定数组
你可以使用 `Type[N]` 来创建一个数组。

N必须是一个编译期就能够确定的常量，而不能由`let/var`定义的变量来提供具体的长度，N的类型为usize。

```cns
const COUNT: usize = { let half: usize = 2; half + 1 };
const SMALL: u32 = 3;

fn main() -> i32 {
    let first: i32[COUNT] = [10, 11, 21];
    let second: i32[SMALL as usize] = [7; 3];
    let empty: i32[0] = [];
    let impossible_elements: ![0] = [];
    let len = first.len;
    return first[0] + first[1] + first[2];
}
```

`[]` 和 `[10, 11, 21]`是数组的初始化列表。

`[7 ; 3]` 则表示[7, 7, 7]，即 `[表达式; 数组长度]`，先求表达式一次，然后根据长度创建对应的数组，
实际上 `[7 ; 0]` 也是允许的。

`![0]`，表示一个类型为`!`的空数组，但不表示空列表的类型就是`![0]`。

`[abort(); 0];`，也支持这种写法，但此操作不会构造空数组，因为程序已经终止。

`first.len`，可以用来获取数组的长度。

### 动态下标

对于使用动态下标(例如使用`let index = 1; arr[index]`)的数组，当前阶段有相对多的规则限制，具体规则请在使用时进行摸索，这些限制会在后续逐步移除。

例如说，在进行动态读取之前，进行了部分移出`move arr[index]`，此处再次进行`arr[index]`会被拒绝，即使index已经发生改变。

### Copy与Move

数组整体Copy还是Move由存储的Type决定，只有整个数组所有元素都具有Copy的能力时，数组才具有Copy的能力，否则数组将以Move的形式进行传递。

| 数组类型 | Copy 资格 | 普通取值 `let b = a` |
| --- | --- | --- |
| `T[0]` | 有；T 仍须合法、有限 | 复制，保留 a |
| `T[N]`，N>0，T 是 Copy | 有 | 复制，保留 a |
| `T[N]`，N>0，T 非 Copy | 没有 | 移动，a 整体失效 |

对于具有 Copy 资格的数组，仍可以显式移动：

```cns
let a = [10, 20];

let b = a;      // 复制整个数组，a 有效
let c = copy a; // 显式复制，a 有效
let d = move a; // 显式移动，即使 i32[2] 是 Copy，也消费 a

// let e = a;   // 拒绝：a 已移出
```

局部操作按“选中位置的类型”判断，不要求整个数组是 Copy：

| 操作 | 固定下标 `a[0]` | 动态下标 `a[index]` |
| --- | --- | --- |
| 普通取得 Copy 元素 | 复制，保留元素 | 允许，但要求整个数组完整 |
| 普通取得非 Copy 元素 | 移动该元素 | 拒绝 |
| `copy` 元素 | 元素必须 Copy | 元素必须 Copy，且整个数组完整 |
| `move` 元素 | 允许，即使元素 Copy 也消费位置 | 始终拒绝，包括 Copy 元素 |
| 替换元素 | 可写位置可以恢复该固定元素 | 要求整个数组完整且可写 |