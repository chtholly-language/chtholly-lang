// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Numeric/numeric.h>
#include <gtest/gtest.h>
#include <stdexcept>

namespace Chtholly::Testing {
namespace {
using T = Target::Scalar;
using O = Numeric::Operation;
using F = Numeric::Failure;
auto Integer(std::string_view value, T type,
             Numeric::Budget &budget) -> Numeric::Value {
  const bool negative = value.starts_with('-');
  if (negative) {
    value.remove_prefix(1);
  }
  const auto result = Numeric::Literal(value, 10, type, negative, budget);
  if (!result) {
    throw std::logic_error("invalid numeric test vector");
  }
  return *result.ValueResult;
}
auto Mathematics(std::string_view value,
                 Numeric::Budget &budget) -> Numeric::Value {
  const bool negative = value.starts_with('-');
  if (negative) {
    value.remove_prefix(1);
  }
  const auto result = Numeric::MathematicalLiteral(value, 10, negative, budget);
  if (!result) {
    throw std::logic_error("invalid mathematical fixture");
  }
  return *result.ValueResult;
}
} // namespace
TEST(numeric, type_identity_and_integer_ranges) {
  EXPECT_NE(T::ISize, T::I64);
  EXPECT_EQ(Target::WindowsX64.bits(T::ISize), 64U);
  EXPECT_FALSE(Target::IsCopy(T::Never));
  Numeric::Budget budget;
  for (const auto type : {T::I8, T::I16, T::I32, T::I64, T::I128, T::U8, T::U16,
                          T::U32, T::U64, T::U128, T::ISize, T::USize}) {
    const auto zero = Integer("0", type, budget);
    EXPECT_EQ(zero.type(), type);
    EXPECT_EQ(zero.bits()[0], 0U);
    EXPECT_EQ(zero.bits()[1], 0U);
  }
  EXPECT_TRUE(Numeric::Literal("128", 10, T::I8, true, budget));
  EXPECT_EQ(Numeric::Literal("128", 10, T::I8, false, budget).Error,
            F::OutOfRange);
  EXPECT_EQ(
      Integer("340282366920938463463374607431768211455", T::U128, budget)
          .bits(),
      (std::array<std::uint64_t, 2>{~std::uint64_t{0}, ~std::uint64_t{0}}));
  EXPECT_EQ(Integer("-170141183460469231731687303715884105728", T::I128, budget)
                .bits()[1],
            0x8000000000000000ULL);
}
TEST(numeric, leading_zeroes_preserve_budget_and_value) {
  Numeric::Budget budget;
  for (const auto radix : {2U, 8U, 10U, 16U}) {
    for (const auto mathematical : {false, true}) {
      const auto form = [&](std::string digits, bool negative) {
        return mathematical
                   ? Numeric::MathematicalLiteral(digits, radix, negative,
                                                  budget)
                   : Numeric::Literal(digits, radix, T::I32, negative, budget);
      };
      const auto zero = form(std::string(4096, '0'), false);
      ASSERT_TRUE(zero);
      EXPECT_EQ(zero.ValueResult->describe(), "0");
      const auto one = form(std::string(4095, '0') + "1", true);
      ASSERT_TRUE(one);
      EXPECT_EQ(one.ValueResult->describe(), "-1");
      EXPECT_EQ(form(std::string(4096, '0') + "1", false).Error,
                F::ResourceExceeded);
    }
  }
  const auto answer = Numeric::Literal("00042", 10, T::I32, false, budget);
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer.ValueResult->bits()[0], 42U);
}
TEST(numeric, signed_zero_to_every_integer) {
  Numeric::Budget budget;
  for (const auto source : {T::F32, T::F64}) {
    const auto sign = source == T::F32 ? 0x80000000ULL : 0x8000000000000000ULL;
    for (const auto bits : {0ULL, sign}) {
      const auto value = Numeric::Value::FromBits(source, {bits, 0});
      for (const auto type :
           {T::I8, T::I16, T::I32, T::I64, T::I128, T::U8, T::U16, T::U32,
            T::U64, T::U128, T::ISize, T::USize}) {
        const auto converted = Numeric::Convert(value, type, budget);
        ASSERT_TRUE(converted);
        EXPECT_EQ(converted.ValueResult->type(), type);
        EXPECT_EQ(converted.ValueResult->bits(),
                  (std::array<std::uint64_t, 2>{0, 0}));
      }
    }
  }
}
TEST(numeric, arithmetic_and_bit_patterns) {
  Numeric::Budget budget;
  const auto minimum = Integer("-128", T::I8, budget);
  const auto minusOne = Integer("-1", T::I8, budget);
  EXPECT_EQ(Numeric::Binary(O::Divide, minimum, minusOne, budget).Error,
            F::Overflow);
  const auto remainder =
      Numeric::Binary(O::Remainder, minimum, minusOne, budget);
  ASSERT_TRUE(remainder);
  EXPECT_EQ(remainder.ValueResult->bits()[0], 0U);
  EXPECT_EQ(Numeric::Binary(O::Add, Integer("255", T::U8, budget),
                            Integer("1", T::U8, budget), budget)
                .Error,
            F::Overflow);
  const auto shifted =
      Numeric::Binary(O::ShiftLeft, Integer("255", T::U8, budget),
                      Integer("1", T::U128, budget), budget);
  ASSERT_TRUE(shifted);
  EXPECT_EQ(shifted.ValueResult->bits()[0], 254U);
  EXPECT_EQ(Numeric::Binary(O::ShiftLeft, Integer("1", T::U8, budget),
                            Integer("8", T::U64, budget), budget)
                .Error,
            F::ShiftCount);
  EXPECT_EQ(Numeric::Binary(O::ShiftRight, minimum,
                            Integer("-1", T::I32, budget), budget)
                .Error,
            F::ShiftCount);
  const auto negativeRemainder =
      Numeric::Binary(O::Remainder, Integer("-7", T::I32, budget),
                      Integer("3", T::I32, budget), budget);
  ASSERT_TRUE(negativeRemainder);
  EXPECT_EQ(negativeRemainder.ValueResult->describe(), "-1");
}
TEST(numeric, conversion_order_and_finite_range) {
  Numeric::Budget budget;
  EXPECT_EQ(
      Numeric::Convert(Integer("16777217", T::I32, budget), T::F32, budget)
          .Error,
      F::PrecisionLoss);
  // M+1 exceeds f32's finite mathematical range even though it rounds to M.
  EXPECT_EQ(Numeric::Convert(Integer("340282346638528859811704183484516925441",
                                     T::U128, budget),
                             T::F32, budget)
                .Error,
            F::OutOfRange);
  const auto aboveMaximum =
      Numeric::Value::FromBits(T::F64, {0x47efffffe0000001ULL, 0});
  EXPECT_EQ(Numeric::Convert(aboveMaximum, T::F32, budget).Error,
            F::OutOfRange);
  const auto infinity =
      Numeric::Value::FromBits(T::F64, {0x7ff0000000000000ULL, 0});
  const auto fractional =
      Numeric::Value::FromBits(T::F64, {0x3ff8000000000000ULL, 0});
  EXPECT_EQ(Numeric::Convert(infinity, T::I8, budget).Error, F::NonFinite);
  EXPECT_EQ(Numeric::Convert(fractional, T::I8, budget).Error, F::Fractional);
  const auto hugeFraction =
      Numeric::Value::FromBits(T::F64, {0x4070080000000000ULL, 0});
  EXPECT_EQ(Numeric::Convert(hugeFraction, T::I8, budget).Error, F::Fractional);
  const auto negativeZero =
      Numeric::Value::FromBits(T::F64, {0x8000000000000000ULL, 0});
  const auto zero = Numeric::Convert(negativeZero, T::F32, budget);
  ASSERT_TRUE(zero);
  EXPECT_EQ(zero.ValueResult->bits()[0], 0x80000000U);
  const auto integerZero = Numeric::Convert(negativeZero, T::U128, budget);
  ASSERT_TRUE(integerZero);
  EXPECT_EQ(integerZero.ValueResult->bits()[0], 0U);
}
TEST(numeric, ieee_literal_and_arithmetic_policies) {
  Numeric::Budget budget;
  const auto tenth = Numeric::Literal("0.1", 10, T::F32, false, budget);
  ASSERT_TRUE(tenth);
  EXPECT_EQ(tenth.ValueResult->bits()[0], 0x3dcccccdU);
  EXPECT_EQ(Numeric::Literal("1e1000", 10, T::F32, false, budget).Error,
            F::OutOfRange);
  const auto maximumNeighbor = Numeric::Literal(
      "340282346638528859811704183484516925441", 10, T::F32, false, budget);
  ASSERT_TRUE(maximumNeighbor);
  EXPECT_EQ(maximumNeighbor.ValueResult->bits()[0], 0x7f7fffffU);
  const auto decimalMinimum = Numeric::Literal(
      "1."
      "401298464324817070923729583289916131280261941876515771757068283889791082"
      "68586060148663818836212158203125e-45",
      10, T::F32, false, budget);
  ASSERT_TRUE(decimalMinimum);
  EXPECT_EQ(decimalMinimum.ValueResult->bits()[0], 1U);
  const auto decimalHalf = Numeric::Literal(
      "7."
      "006492321624085354618647916449580656401309709382578858785341419448955413"
      "42930300743319094181060791015625e-46",
      10, T::F32, false, budget);
  ASSERT_TRUE(decimalHalf);
  EXPECT_EQ(decimalHalf.ValueResult->bits()[0], 0U);
  const auto tiny = Numeric::Literal("1e-1000", 10, T::F32, true, budget);
  ASSERT_TRUE(tiny);
  EXPECT_EQ(tiny.ValueResult->bits()[0], 0x80000000U);
  const auto minimum = Numeric::Value::FromBits(T::F32, {1, 0});
  const auto two = Numeric::Value::FromBits(T::F32, {0x40000000, 0});
  const auto half = Numeric::Binary(O::Divide, minimum, two, budget);
  ASSERT_TRUE(half);
  EXPECT_EQ(half.ValueResult->bits()[0], 0U);
  const auto zero = Numeric::Value::FromBits(T::F32, {0, 0});
  const auto nan = Numeric::Binary(O::Divide, zero, zero, budget);
  ASSERT_TRUE(nan);
  EXPECT_EQ(
      Numeric::Binary(O::Equal, *nan.ValueResult, *nan.ValueResult, budget)
          .ValueResult->boolean(),
      false);
  EXPECT_EQ(
      Numeric::Binary(O::NotEqual, *nan.ValueResult, *nan.ValueResult, budget)
          .ValueResult->boolean(),
      true);
}
TEST(numeric, mathematical_values_and_resource_limits) {
  Numeric::Budget budget;
  const auto large =
      Mathematics("340282366920938463463374607431768211456", budget);
  const auto one = Mathematics("1", budget);
  const auto result = Numeric::Binary(O::Add, large, one, budget);
  ASSERT_TRUE(result);
  EXPECT_EQ(result.ValueResult->describe(),
            "340282366920938463463374607431768211457");
  Numeric::Limits limits;
  limits.LiteralDigits = 3;
  Numeric::Budget small(limits);
  EXPECT_TRUE(Numeric::Literal("999", 10, T::I32, false, small));
  EXPECT_EQ(Numeric::Literal("1000", 10, T::I32, false, small).Error,
            F::ResourceExceeded);
  limits.IntegerBits = 8;
  Numeric::Budget narrow(limits);
  EXPECT_EQ(Numeric::MathematicalLiteral("255", 10, false, narrow).Error,
            F::ResourceExceeded);
  limits.EvaluationSteps = 0;
  Numeric::Budget exhausted(limits);
  EXPECT_EQ(Numeric::Literal("0", 10, T::I32, false, exhausted).Error,
            F::ResourceExceeded);
  EXPECT_FALSE(Numeric::IsRecoverable(F::ResourceExceeded));
}
} // namespace Chtholly::Testing
