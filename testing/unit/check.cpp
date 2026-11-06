// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Testing/frontend.h>
#include <gtest/gtest.h>

namespace Chtholly::Testing {
TEST(check, integer_boundaries) {
  for (const auto *value :
       {"0", "42", "2147483647", "-2147483648", "0x2a", "0b101010", "0o52"}) {
    EXPECT_TRUE(
        Accepts(std::string("fn main() -> i32 { return ") + value + "; }"))
        << value;
  }
  EXPECT_TRUE(Accepts(
      "fn main() -> i32 { let a = -128i8; let b = -(128i8); return 0; }"));
  EXPECT_FALSE(Accepts("fn main() -> i32 { let a = 128i8; return 0; }"));
  EXPECT_FALSE(
      Accepts("fn main() -> i32 { let a = -(128i8 + 0i8); return 0; }"));
  EXPECT_FALSE(Accepts("fn main() -> i32 { let a = --128i8; return 0; }"));
}
TEST(check, rejects_entry_type_and_overflow) {
  for (const auto *text : {
           "fn other() -> i32 { return 0; }",
           "fn main() -> u32 { return 0; }",
           "fn main() -> i32 { return 2147483648; }",
           "fn main(x: i32) -> i32 { return x; }",
       }) {
    EXPECT_FALSE(Accepts(text)) << text;
  }
}
TEST(check, long_decimal) {
  EXPECT_TRUE(
      Accepts("fn main() -> i32 { return " + std::string(4094, '0') + "42; }"));
  EXPECT_FALSE(
      Accepts("fn main() -> i32 { return " + std::string(4095, '0') + "42; }"));
  Numeric::Limits limits;
  limits.LiteralDigits = 2;
  Diagnostic::Diagnostics diagnostics;
  EXPECT_FALSE(
      Checked("fn main() -> i32 { return 123; }", diagnostics, limits));
}
TEST(check, binding_scope_and_fixed_types) {
  for (const auto *text : {
           "fn main() -> i32 { let x = 1; { let x = x + 1; x; } return x; }",
           "fn main() -> i32 { var x = 1; x = { x = 2; x + 1 }; return x; }",
           "fn f(var x: i32) -> i32 { x = x + 1; return x; } "
           "fn main() -> i32 { return f(41); }",
       }) {
    EXPECT_TRUE(Accepts(text)) << text;
  }
  for (const auto *text : {
           "fn main() -> i32 { let x = x; return 0; }",
           "fn main() -> i32 { let x = 1; x = 2; return x; }",
           "fn main() -> i32 { var x = 1; x = 2u8; return 0; }",
           "fn main() -> i32 { let x = 1; let x = 2; return x; }",
           "fn f(x: i32) { let x = 2; } fn main() -> i32 { return 0; }",
           "fn main() -> i32 { let x = 1; let y: u8 = x; return 0; }",
       }) {
    EXPECT_FALSE(Accepts(text)) << text;
  }
}
TEST(check, literals_conversions_and_recovery) {
  for (const auto *text : {
           "fn main() -> i32 { let x: f32 = 1.0; let y = 1f32; return 0; }",
           "fn main() -> i32 { let x = (4294967295 + 1) as u64; return 0; }",
           "fn main() -> i32 { let x = 300 as u8 else { 0 }; return x as i32; "
           "}",
           "fn main() -> i32 { let x = 0 as u8 else { 1 / 0 }; return 0; }",
           "fn main() -> i32 { let x = 1 / 0 else { return 7; }; return x; }",
       }) {
    EXPECT_TRUE(Accepts(text)) << text;
  }
  for (const auto *text : {
           "fn main() -> i32 { let x: f32 = 1; return 0; }",
           "fn main() -> i32 { let x = 1 + 2.0f32; return 0; }",
           "fn main() -> i32 { let x = 300u8 as u8 else { 0 }; return 0; }",
           "fn main() -> i32 { let x = (1 / 0) as u8 else { 0 }; return 0; }",
           "fn main() -> i32 { let x = 0 as u8 else { true }; return 0; }",
           "fn main() -> i32 { let x = 300 as u8 else { 0; }; return 0; }",
           "fn main() -> i32 { let x = 1 / 0 else { 0 }; return 0; }",
           "fn main() -> i32 { let x = 0.1 as f32; return 0; }",
           "fn main() -> i32 { let x = 300 as u8 else { 1 / 0 }; return 0; }",
           "fn f(value: i32) { let x = value as f32 else { 0 }; } "
           "fn main() -> i32 { return 0; }",
           "fn f(value: f32) {} fn main() -> i32 { f(1); return 0; }",
           "fn main() -> i32 { let x = 1 as !; return 0; }",
       }) {
    EXPECT_FALSE(Accepts(text)) << text;
  }
}
TEST(check, unreachable_checks_and_normal_entry) {
  for (const auto *text : {
           "fn f(x: !) -> i32 {} fn main() -> i32 { return 0; }",
           "fn f(x: !) -> i32 { let z = 1 / 0; } fn main() -> i32 { return 0; "
           "}",
           "fn main() -> i32 { false && ((1 / 0) == 0); "
           "if (false) { let z = 1 / 0; } return 0; let z = 1 / 0; }",
           "fn f() -> i32 { abort() } fn main() -> i32 { return 0; }",
       }) {
    EXPECT_TRUE(Accepts(text)) << text;
  }
  for (const auto *text : {
           "fn f(x: !) -> i32 { return true; } fn main() -> i32 { return 0; }",
           "fn f(x: !) -> i32 { unknown(); } fn main() -> i32 { return 0; }",
           "fn f(x: i32) -> i32 {} fn main() -> i32 { return 0; }",
           "fn main() -> i32 { return 0; unknown(); }",
           "fn main() -> i32 { if (false) { let z = 256u8; } return 0; }",
           "fn main() -> i32 { abort() + true; }",
           "fn main() -> i32 { let value: never = abort(); return 0; }",
       }) {
    EXPECT_FALSE(Accepts(text)) << text;
  }
}
TEST(check, labels_shadowing_and_returns) {
  EXPECT_TRUE(
      Accepts("fn f(value: f32) {} fn main() -> i32 { f(1.0); return 0; }"));
  EXPECT_TRUE(Accepts("fn f(label local: i32, _ other: i32) -> i32 { "
                      "return local + other; } "
                      "fn main() -> i32 { return f(40, 2); }"));
  EXPECT_TRUE(Accepts("fn panic() -> i32 { return 42; } "
                      "fn main() -> i32 { return panic(); }"));
  EXPECT_TRUE(Accepts("fn f() { () } fn main() -> i32 { f(); return 0; }"));
  for (const auto *text : {
           "fn main() -> i32 { let abort = 1; abort(); return 0; }",
           "fn f() { 42 } fn main() -> i32 { return 0; }",
           "fn f() -> i32 { 42 } fn main() -> i32 { return 0; }",
           "fn main() -> i32 { if (true) { 42 } return 0; }",
           "fn main() -> i32 { assert(condition: true); return 0; }",
           "fn main() -> i32 { if (1) {} return 0; }",
       }) {
    EXPECT_FALSE(Accepts(text)) << text;
  }
  const auto prefix =
      std::string("fn f(a: i32, b: i32) -> i32 { return a + b; } "
                  "fn main() -> i32 { return ");
  for (const auto *arguments : {"f()", "f(1)", "f(1, 2, 3)", "f(a: 1, a: 2)",
                                "f(c: 1, b: 2)", "f(1, a: 2)"}) {
    EXPECT_FALSE(Accepts(prefix + arguments + "; }")) << arguments;
  }
}
TEST(check, divergence_preserves_static_checks) {
  EXPECT_TRUE(
      Accepts("fn choose(c: bool) -> i32 { "
              "let x = { let a = if (c) { return 1; } else { 42 }; a }; "
              "return x; } fn main() -> i32 { return choose(false); }"));
  EXPECT_TRUE(Accepts("fn choose(c: bool) -> u8 { "
                      "let x = if (c) { 1 } else { let byte = 2u8; byte }; "
                      "return x; } fn main() -> i32 { return 0; }"));
  EXPECT_TRUE(Accepts("fn choose(c: bool) -> i32 { "
                      "let x = if (c) { return 1; } else { 42 }; return x; } "
                      "fn main() -> i32 { return choose(false); }"));
  EXPECT_TRUE(Accepts("fn main() -> i32 { let value: bool = abort() + 1; }"));
  EXPECT_TRUE(Accepts("fn f(value: !) -> bool {} "
                      "fn main() -> i32 { return f(abort()); }"));
  EXPECT_TRUE(
      Accepts("fn main() -> i32 { let value: i32 = { abort(); true }; }"));
  EXPECT_FALSE(Accepts("fn main() -> i32 { abort(); return true; }"));
  EXPECT_TRUE(
      Accepts("fn main() -> i32 { "
              "if (if (true) { false } else { true }) { let x = 1 / 0; } "
              "return 0; }"));
  EXPECT_TRUE(Accepts(
      "fn main() -> i32 { if ({ 0; false }) { let x = 1 / 0; } return 0; }"));
  EXPECT_TRUE(Accepts("fn f(_ a: i32, _ b: i32) -> i32 { return b; } "
                      "fn main() -> i32 { return f(abort(), { panic(); }); }"));
  EXPECT_TRUE(
      Accepts("fn f(value: bool) -> bool { return value && { abort(); }; } "
              "fn main() -> i32 { return 0; }"));
  EXPECT_FALSE(Accepts("fn f(_ a: i32, _ b: i32) -> i32 { return b; } "
                       "fn main() -> i32 { return f(abort(), true); }"));
  EXPECT_FALSE(Accepts("fn f(c: bool) { if (c) { let x = 1 / 0; } } "
                       "fn main() -> i32 { return 0; }"));
  EXPECT_FALSE(Accepts("fn main() -> i32 { let x: isize = 0i64; return 0; }"));
}
TEST(check, rejects_postponed_features) {
  for (const auto *body : {
           "let r = &value;",
           "let x = value as? u8;",
           "const x = 1;",
       }) {
    EXPECT_FALSE(Accepts(std::string("fn main() -> i32 { let value = 0; ") +
                         body + " return 0; }"))
        << body;
  }
  EXPECT_TRUE(
      Accepts("fn main() -> i32 { let interface = 42; return interface; }"));
  EXPECT_TRUE(
      Accepts("fn f(_ unit: ( )) {} fn main() -> i32 { f(()); return 0; }"));
}
} // namespace Chtholly::Testing
