## As

as 用于类型转换，其使用方式为 `源表达式 as 目标类型`。

as使用的转换规则是保守的，先按源表达式自身的规则得到值，再判断转换能否保值。

| 源 → 目标 | 成功条件 | 可能的数值失败 |
| --- | --- | --- |
| 整数 → 整数 | 源数学值在目标范围内 | `Conversion.OutOfRange` |
| 整数 → f32/f64 | 源值在目标有限范围内，且能精确表示 | `Conversion.OutOfRange`、`Conversion.PrecisionLoss` |
| f32/f64 → 整数 | 有限、没有小数部分、在目标范围内 | `Conversion.NonFinite`、`Conversion.Fractional`、`Conversion.OutOfRange` |
| f32 → f64 | 所有值均能保留；NaN 保留类别 | 无规定的数值失败 |
| f64 → f32 | 有限值必须在范围内且精确；特殊值按类别保留 | `Conversion.OutOfRange`、`Conversion.PrecisionLoss` |
| 数值 → 同一个数值类型 | 保留已有值；NaN 保留类别 | 无规定的数值失败 |
| bool、unit 值参与转换，或以 `!` 为转换目标 | 不属于合法数值转换 | 静态拒绝，不能靠 else 恢复 |

`as` 不提供截断、饱和、按模回绕或按位重解释。

例如 255u8 转 i8 不会变成 -1，1.9 转 i32 不会变成 1。

### 可恢复错误

下述错误属于可恢复错误，恢复方式有两种

```cns
// 转换失败：可以提供替代值，然后继续。
let value = input as u8 else { 0 };

// 整数算术失败：必须提前返回或终止。
let sum = left + right else { return 0; };
```

| 类别 | 触发条件 | 例子 |
| --- | --- | --- |
| `Arithmetic.Overflow` | 整数加减乘、取负的结果越界，或最小有符号整数除以 -1 | `127i8 + 1i8`、`-1u8` |
| `Arithmetic.DivisionByZero` | 整数除法或余数的除数为零 | `1 / 0`、`1 % 0` |
| `Arithmetic.ShiftCount` | 位移计数为负，或不小于左操作数位宽 | `1u8 << 8` |
| `Conversion.OutOfRange` | 源值超出转换目标的范围 | `300 as u8` |
| `Conversion.PrecisionLoss` | 源值在目标范围内，但不能精确表示 | `16777217 as f32`、`0.1f64 as f32` |
| `Conversion.NonFinite` | 将 NaN 或正负无穷转换成整数 | 浮点无穷转 i32 |
| `Conversion.Fractional` | 浮点转整数时带有非零小数部分 | `1.5f64 as i32` |

详细的类型说明，请阅读[Type](./Type.md)文档
有关字面量方面，请阅读[Literal](./Literal.md)文档