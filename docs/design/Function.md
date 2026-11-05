## 函数

Chtholly的基本函数形状如下，函数支持前向调用、直接递归和互递归。

```cns
fn add(x: i32, y: i32) -> i32 {
    return x + y;
}
```

存在返回类型的函数必须通过`return`的方式返回，不支持尾随表达式(即Rust的作用域自动返回最后一个无分号表达式)。

## 重载

Chtholly暂不支持重载。

## 位置与标签

Chtholly的参数列表允许下述写法。

| 形参 | 调用方式 |
| --- | --- |
| `name: T` | 位置或 `name: expression` |
| `label local: T` | 位置或 `label: expression`，函数体使用 local |
| `_ local: T` | 只允许位置 |

目前位置参数之前不能出现标签。

```cns
fn function(x: i32, _ y: i32, label z: i32) {

}

function(10, 20, 30);           // 合法：全部按位置
function(x: 10, 20, 30);        // 拒绝：位置参数之前不能出现标签参数
function(x: 10, 20, label: 30); // 拒绝：同上
function(label: 10, 20, x: 30); // 拒绝：同上
```

## `!`

`!`类型是没有任何合法值的类型，即用于表达此路径不能正常完成并交出一个值。

`!`可以用于函数参数或作为返回值类型，这两种情况所表示的语义并不同。

当`!`作为函数参数时，表示函数没有正常入口。

```cns
fn function(v: !) {

}

function(about());
```

当`!`作为返回值类型时，表示函数不会正常返回。

```cns
fn function() -> ! {
    panic();
}
```