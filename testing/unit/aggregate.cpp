// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Target/layout.h>
#include <Chtholly/Testing/frontend.h>
#include <gtest/gtest.h>
#include <sstream>
#include <stdexcept>

namespace Chtholly::Testing {
namespace {
auto Runs(std::string_view text) -> bool {
  Diagnostic::Diagnostics diagnostics;
  return Executable(text, diagnostics).has_value();
}
auto FlowRejects(std::string_view text, std::string_view reason) -> bool {
  Diagnostic::Diagnostics diagnostics;
  const auto checked = Checked(text, diagnostics);
  if (!checked) {
    return false;
  }
  const auto ir = Lower::Lower(*checked, diagnostics);
  if (!ir || Flow::Analyze(*ir, diagnostics)) {
    return false;
  }
  std::ostringstream output;
  diagnostics.render(output);
  return output.str().find(reason) != std::string::npos;
}
auto Main(std::string body) -> std::string {
  return "fn main() -> i32 { " + body + " return 0; }";
}
} // namespace

TEST(aggregate, nominal_alias_and_tuple_identity) {
  EXPECT_TRUE(
      Runs("struct A { x: i32 } alias Same = A; fn take(_ a: Same) {} " +
           Main("take(A { x: 1 });")));
  EXPECT_FALSE(
      Accepts("struct A { x: i32 } struct B { x: i32 } fn take(_ a: A) {} " +
              Main("take(B { x: 1 });")));
  EXPECT_FALSE(
      Accepts("struct A { x: i32 } fn take(_ a: A) {} " + Main("take((1,));")));
  Types::Builder left;
  Types::Builder right;
  const auto a = left.declare(Types::Kind::Struct, "A");
  left.define(a, {});
  const auto b = right.declare(Types::Kind::Struct, "A");
  right.define(b, {});
  std::vector<Types::Error> errors;
  const auto table = left.finish(errors);
  ASSERT_TRUE(table);
  EXPECT_NE(a, b);
  EXPECT_FALSE(table->contains(b));
  Types::Builder malformed;
  EXPECT_THROW(malformed.declare(Types::Kind::Tuple, "fake"), std::logic_error);
  const auto structure = malformed.declare(Types::Kind::Struct, "S");
  EXPECT_THROW(malformed.define(structure, {}, {{"V", {}, false}}),
               std::logic_error);
}
TEST(aggregate, const_lengths_and_cycles) {
  EXPECT_TRUE(Runs("const N: usize = { let half: usize = 2; half * 2 }; " +
                   Main("let a: i32[N] = [1; N]; let n = a.len;")));
  EXPECT_TRUE(Runs("const N: usize = M + 1; const M: usize = 1; " +
                   Main("let a: i32[N] = [1, 2];")));
  EXPECT_FALSE(Accepts("const N: u32 = 2; " + Main("let a: i32[N] = [1, 2];")));
  EXPECT_TRUE(
      Runs("const N: u32 = 2; " + Main("let a: i32[N as usize] = [1, 2];")));
  EXPECT_FALSE(Accepts("const N: usize = M; const M: usize = N; " + Main("")));
  EXPECT_FALSE(Accepts("alias A = B; alias B = A; " + Main("")));
  EXPECT_FALSE(
      Accepts("const N: usize = { var x: usize = 1; x }; " + Main("")));
  EXPECT_FALSE(
      Accepts("fn value() -> usize { return 1; } const N: usize = value(); " +
              Main("")));
}
TEST(aggregate, zero_arrays_and_uninhabited_entry) {
  EXPECT_TRUE(Runs(Main("let empty: ![0] = []; let n = empty.len;")));
  EXPECT_FALSE(Accepts("struct S { child: S[0] } " + Main("")));
  EXPECT_TRUE(Runs("enum Void {} fn f(_ value: Void) -> i32 {} " + Main("")));
  EXPECT_FALSE(Accepts(
      "enum Void {} fn f(_ value: Void) -> i32 { return true; } " + Main("")));
  EXPECT_FALSE(Accepts("fn f(_ value: ![0]) -> i32 {} " + Main("")));
  EXPECT_FALSE(Accepts(
      "enum Void {} fn f(_ value: Void) -> i32 { unknown(); } " + Main("")));
}
TEST(aggregate, checked_copy_evidence) {
  EXPECT_TRUE(Runs("struct A { x: i32 } impl core::Copy for A {} " +
                   Main("let a = A { x: 1 }; let b = a; let c = a;")));
  EXPECT_TRUE(FlowRejects("struct A { x: i32 } " +
                              Main("let a = A { x: 1 }; let b = a; let c = a;"),
                          "fully initialized"));
  EXPECT_FALSE(Accepts("struct Token {} struct A { token: Token } "
                       "impl core::Copy for A {} " +
                       Main("")));
  EXPECT_FALSE(Accepts("struct A {} alias B = A; impl core::Copy for A {} "
                       "impl core::Copy for B {} " +
                       Main("")));
  EXPECT_FALSE(Accepts("struct A { impossible: ! } impl core::Copy for A {} " +
                       Main("")));
  EXPECT_TRUE(Runs("struct A {} impl core :: Copy for A {} " +
                   Main("let a = A {}; let b = copy a;")));
  EXPECT_FALSE(
      Accepts("fn core() {} struct A {} impl core::Copy for A {} " + Main("")));
}
TEST(aggregate, partial_move_and_restoration) {
  const std::string declarations =
      "struct Token { value: i32 } "
      "struct Pair { first: Token, second: Token } ";
  EXPECT_TRUE(
      Runs(declarations +
           Main("var p = Pair { first: Token { value: 1 }, "
                "second: Token { value: 2 } }; let first = p.first; "
                "let other = p.second.value; p.first = Token { value: 3 }; "
                "let whole = p;")));
  EXPECT_FALSE(Accepts(
      declarations + Main("let p = Pair { first: Token { value: 1 }, "
                          "second: Token { value: 2 } }; let first = p.first; "
                          "p.first = Token { value: 3 };")));
  EXPECT_TRUE(FlowRejects(declarations +
                              Main("let p = Pair { first: Token { value: 1 }, "
                                   "second: Token { value: 2 } }; "
                                   "let first = p.first; let whole = p;"),
                          "fully initialized"));
}
TEST(aggregate, explicit_move_and_computed_results) {
  EXPECT_TRUE(FlowRejects(Main("let x = 1; let y = move (x); let z = x;"),
                          "fully initialized"));
  EXPECT_TRUE(Runs(Main("let x = 1; let y = move (x as i32); let z = x;")));
  EXPECT_TRUE(FlowRejects(Main("let x = 1; let y = move x as i32; let z = x;"),
                          "fully initialized"));
}
TEST(aggregate, branch_join_ignores_returns) {
  EXPECT_TRUE(FlowRejects("struct T {} fn f(c: bool) { let t = T {}; "
                          "if (c) { let x = t; } let y = t; } " +
                              Main(""),
                          "fully initialized"));
  EXPECT_TRUE(Runs("struct T {} fn f(c: bool) { let t = T {}; "
                   "if (c) { let x = t; return; } let y = t; } " +
                   Main("")));
}
TEST(aggregate, zero_sized_paths_are_distinct) {
  EXPECT_TRUE(Runs("struct Z {} struct Pair { a: Z, b: Z } " +
                   Main("var p = Pair { a: Z {}, b: Z {} }; "
                        "let a = p.a; let b = p.b; p.a = Z {}; "
                        "p.b = Z {}; let whole = p;")));
  EXPECT_TRUE(FlowRejects("struct Z {} struct Pair { a: Z, b: Z } " +
                              Main("let p = Pair { a: Z {}, b: Z {} }; "
                                   "let a = p.a; let duplicate = p.a;"),
                          "fully initialized"));
}
TEST(aggregate, construction_order_is_explicit) {
  const auto text = "struct Pair { a: i32, b: i32 } " +
                    Main("var trace = 0; let p = Pair { b: { trace = 2; 2 }, "
                         "a: { trace = 21; 1 } }; "
                         "let a = [ { trace = 3; 1 }, { trace = 34; 2 } ];");
  EXPECT_TRUE(Runs(text));
  EXPECT_FALSE(Accepts("struct Pair { a: i32, b: i32 } " +
                       Main("let p = Pair { a: 1, a: 2 };")));
}
TEST(aggregate, repeat_seed_and_copy_boundaries) {
  EXPECT_TRUE(Runs("struct T {} " +
                   Main("let zero: T[0] = [T {}; 0]; let one = [T {}; 1];")));
  EXPECT_FALSE(Accepts("struct T {} " + Main("let two = [T {}; 2];")));
  EXPECT_TRUE(Runs(Main("let a: i32[0] = [abort(); 0];")));
}
TEST(aggregate, fixed_and_dynamic_elements) {
  EXPECT_TRUE(Runs("struct T {} const I: usize = 0; " +
                   Main("var a: T[2] = [T {}, T {}]; let x = a[I]; "
                        "a[I] = T {}; let whole = a;")));
  EXPECT_TRUE(Runs("struct T {} " + Main("var a = [T {}, T {}]; "
                                         "let x = a[{ let i: usize = 0; i }]; "
                                         "a[0] = T {}; let whole = a;")));
  EXPECT_FALSE(
      Accepts("struct T {} " +
              Main("let a: T[2] = [T {}, T {}]; let i = 0; let x = a[i];")));
  EXPECT_FALSE(Accepts(Main("let a = [1, 2]; let i = 0; let x = move a[i];")));
  EXPECT_FALSE(Accepts(Main("let a = [1, 2]; let x = a[-1];")));
  EXPECT_FALSE(
      Accepts(Main("let a = [1, 2]; let x = a[18446744073709551616u128];")));
  EXPECT_FALSE(Accepts("struct T { n: i32 } " +
                       Main("let a = [T { n: 1 }, T { n: 2 }]; "
                            "let i = 0; let n = move a[i].n;")));
  EXPECT_FALSE(Accepts("fn f(_ impossible: !) { let a = [1, 2]; "
                       "let i = 0; let x = move a[i]; } " +
                       Main("")));
  EXPECT_TRUE(Runs(Main("let a = [1, 2]; let i = 0; "
                        "let x = move (a[i] as i32); let y = a[i];")));
}
TEST(aggregate, moved_array_length_and_dynamic_reads) {
  EXPECT_TRUE(Runs(Main("let a = [1, 2]; let x = move a[0]; "
                        "let n = a.len; let y = a[1];")));
  EXPECT_TRUE(Runs(Main("let a = [1, 2]; let b = move a; let n = a.len;")));
  EXPECT_TRUE(FlowRejects(
      Main("let a = [1, 2]; let x = move a[0]; let i = 1; let y = a[i];"),
      "fully initialized"));
  EXPECT_TRUE(
      FlowRejects("struct T { n: i32 } " +
                      Main("let a = [T { n: 1 }, T { n: 2 }]; "
                           "let first = a[0]; let i = 0; let n = a[i].n;"),
                  "fully initialized array"));
}
TEST(aggregate, index_base_protection) {
  EXPECT_TRUE(FlowRejects(Main("var a = [1, 2]; let x = a[{ a = [3, 4]; 0 }];"),
                          "protected base"));
  EXPECT_TRUE(Runs(
      Main("let a = [1, 2]; var other = 0; let x = a[{ other = 1; 0 }];")));
  EXPECT_TRUE(Runs(Main("let a = [1, 2]; let x = a[{ return 42; 0 }];")));
  EXPECT_TRUE(Runs(Main("let a = [1, 2]; loop { let x = a[{ break; 0 }]; }")));
  EXPECT_TRUE(Runs("fn f() -> i32 { var a = [0]; a[{ abort(); 0 }] = 1; } " +
                   Main("")));
  EXPECT_TRUE(Runs("fn f() -> i32 { var a = [0]; a[{ abort(); 0 }] = 1; "
                   "let x = 1 / 0; } " +
                   Main("")));
  EXPECT_TRUE(Runs("fn f() -> i32 { var a: u8[1] = [0]; "
                   "a[{ abort(); 0 }] = 255; } " +
                   Main("")));
  EXPECT_TRUE(Runs("fn f() -> i32 { var a: u8[1][0] = [[]]; "
                   "a[{ abort(); 0 }] = []; } " +
                   Main("")));
  EXPECT_FALSE(Accepts("fn f() -> i32 { var a: u8[1] = [0]; "
                       "a[{ abort(); 0 }] = 256; } " +
                       Main("")));
}
TEST(aggregate, nested_match_coverage) {
  EXPECT_TRUE(Runs("enum E { V { a: bool, b: bool }, Empty } "
                   "fn f(_ e: E) { match (e) { E::V { a: true, b: _ } => (), "
                   "E::V { a: false, b: _ } => (), E::Empty => (), }; } " +
                   Main("")));
  EXPECT_FALSE(Accepts("fn f(_ p: (bool, bool)) { match (p) { "
                       "(true, _) => (), (false, true) => (), }; } " +
                       Main("")));
  EXPECT_FALSE(
      Accepts("fn f(_ n: u8) { match (n) { 0 => (), }; } " + Main("")));
}
TEST(aggregate, uninhabited_field_permutations) {
  for (const auto *fields : {"a: !, b: bool, c: i32", "a: bool, b: !, c: i32",
                             "a: bool, b: i32, c: !"}) {
    EXPECT_TRUE(Runs(std::string("enum E { Impossible { ") + fields +
                     " }, Empty } fn f(_ e: E) { "
                     "match (e) { E::Empty => (), }; } " +
                     Main("")));
  }
  EXPECT_TRUE(
      Runs("enum Void {} fn f(_ v: Void) -> i32 { return match (v) {}; } " +
           Main("")));
}
TEST(aggregate, coverage_budget_is_not_a_counterexample) {
  Diagnostic::Diagnostics diagnostics;
  const auto checked = Checked(
      "fn f(_ b: bool) { match (b) { true => (), false => (), }; } " + Main(""),
      diagnostics, {}, {0, 256});
  EXPECT_FALSE(checked);
  std::ostringstream output;
  diagnostics.render(output);
  EXPECT_NE(output.str().find("resource budget"), std::string::npos);
  EXPECT_EQ(output.str().find("constructible shape"), std::string::npos);
}
TEST(aggregate, or_alternatives_and_guard) {
  EXPECT_TRUE(Runs("enum E { A { x: i32 }, B { x: i32 } } "
                   "fn f(_ e: E) -> i32 { return match (e) { "
                   "E::A { x } | E::B { x } if (x > 0) => x, _ => 0, }; } " +
                   Main("")));
  EXPECT_FALSE(Accepts("fn f(_ b: bool) { match (b) { var x | x => (), }; } " +
                       Main("")));
  Diagnostic::Diagnostics diagnostics;
  const auto checked =
      Executable("fn f(_ flag: bool) { "
                 "let ((true, a, b) | (false, b, a)) = (flag, 1, 2); } " +
                     Main(""),
                 diagnostics);
  ASSERT_TRUE(checked);
  const auto &groups = checked->storage().Functions[0].BindingGroups;
  ASSERT_EQ(groups.size(), 1U);
  ASSERT_EQ(groups[0].CreationOrders.size(), 2U);
  ASSERT_EQ(groups[0].CreationOrders[0].size(), 2U);
  EXPECT_EQ(groups[0].CreationOrders[0][0], groups[0].CreationOrders[1][1]);
  EXPECT_EQ(groups[0].CreationOrders[0][1], groups[0].CreationOrders[1][0]);
}
TEST(aggregate, trial_bindings_and_external_effects) {
  EXPECT_FALSE(Accepts("fn f(_ b: bool) { match (b) { "
                       "var x if ({ x = false; true }) => (), _ => (), }; } " +
                       Main("")));
  EXPECT_FALSE(Accepts("struct T {} fn take(_ x: T) -> bool { return true; } "
                       "fn f(_ t: T) { match (t) { "
                       "x if (take(x)) => (), _ => (), }; } " +
                       Main("")));
  EXPECT_TRUE(Runs("fn f(_ b: bool) { var other = 0; match (b) { "
                   "x if ({ other = 1; false }) => (), _ => (), }; } " +
                   Main("")));
}
TEST(aggregate, destructuring_permissions_and_negative_patterns) {
  EXPECT_TRUE(Runs(Main("let (var a, b) = (1, 2); a = 3; "
                        "var (var c, d) = (3, 4); c = 5; d = 6; "
                        "let (first, .., last) = (1, 2, 3);")));
  EXPECT_TRUE(Runs(Main("let (..,) = ();")));
  EXPECT_FALSE(Accepts(Main("let (a, b) = (1, 2); a = 3;")));
  EXPECT_TRUE(Runs("struct S { first: i32, second: i32 } " +
                   Main("let S { .., var first } = S { first: 1, second: 2 }; "
                        "first = 3;")));
  EXPECT_TRUE(
      Runs("enum E { V { first: i32, second: bool } } " +
           Main("let E::V { first, .. } = E::V { first: 1, second: true };")));
  EXPECT_FALSE(Accepts(Main("let .. = (1, 2);")));
  EXPECT_FALSE(Accepts(Main("let var (a, b) = (1, 2);")));
  EXPECT_TRUE(Runs("fn f(_ n: i8) -> i32 { return match (n) { "
                   "-(128i8) => 1, _ => 0, }; } " +
                   Main("")));
  EXPECT_FALSE(Accepts("fn f(_ n: i8) { match (n) { --1 => (), _ => (), }; } " +
                       Main("")));
  EXPECT_FALSE(Accepts("fn f(_ n: u8) { match (n) { "
                       "256u8 => (), 257u8 => (), _ => (), }; } " +
                       Main("")));
  EXPECT_FALSE(Accepts(Main("let 256u8 | 257u8 = 1u8;")));
}
TEST(aggregate, loop_values_and_labels) {
  EXPECT_TRUE(
      Runs(Main("let n: i32 = outer: loop { loop { break @outer 42; } };")));
  EXPECT_FALSE(Accepts(Main("while (true) { break 1; }")));
  EXPECT_FALSE(Accepts(Main("continue @missing;")));
  EXPECT_FALSE(Accepts(Main("same: loop { same: loop { break; } break; }")));
  EXPECT_TRUE(Runs("fn forever() -> ! { loop {} } " + Main("")));
  EXPECT_FALSE(Accepts("fn f() -> i32 { while (true) {} } " + Main("")));
}
TEST(aggregate, loop_fixed_point) {
  EXPECT_TRUE(
      FlowRejects("struct T {} fn f(_ condition: bool) { let value = T {}; "
                  "while (condition) { let moved = value; } } " +
                      Main(""),
                  "fully initialized"));
  EXPECT_TRUE(Runs("struct T {} fn f(_ condition: bool) { var value = T {}; "
                   "while (condition) { let moved = value; value = T {}; } "
                   "let final = value; } " +
                   Main("")));
}
TEST(aggregate, loop_hints_respect_lexical_bindings) {
  EXPECT_TRUE(Runs(Main("let n = false; let r = loop { let n = 42; "
                        "break n; }; return r;")));
  EXPECT_TRUE(Runs(Main("let n = false; let r: i32 = loop { let n = 42; "
                        "break n; }; return r;")));
  EXPECT_TRUE(Runs(Main("let r = loop { let n = 42; break n; }; return r;")));
  EXPECT_TRUE(Runs("struct S { x: i32 } " +
                   Main("let x = false; let r = outer: loop { "
                        "let S { x } = S { x: 42 }; "
                        "loop { break @outer x; } }; return r;")));
  EXPECT_TRUE(Runs(Main("let r = outer: loop { "
                        "break { break @outer 42; false }; }; return r;")));
  EXPECT_TRUE(Runs(Main("let n = false; let r = loop { "
                        "match ((42,)) { (n,) => { break n; }, } "
                        "}; return r;")));
}
TEST(aggregate, divergent_break_does_not_set_result_type) {
  for (const auto declaration : {"enum Void {} ", "struct Void { x: ! } "}) {
    const auto prefix =
        std::string(declaration) + "fn stop() -> Void { abort(); } ";
    EXPECT_TRUE(Runs(prefix +
                     "fn f(_ c: bool) -> i32 { let r = loop { "
                     "if (c) { break stop(); } "
                     "let v = 42; break v; }; return r; } " +
                     Main("return f(false);")));
    EXPECT_TRUE(Runs(prefix +
                     "fn f(_ c: bool) -> i32 { let r = loop { "
                     "if (c) { break { let dead = stop(); 0u8 }; } "
                     "let v = 42; break v; }; return r; } " +
                     Main("return f(false);")));
  }
  EXPECT_TRUE(Runs("fn f(_ c: bool) -> i32 { let r = loop { "
                   "if (c) { break panic(); } let v = 42; break v; }; "
                   "return r; } " +
                   Main("return f(false);")));
  EXPECT_TRUE(Runs("fn f(_ c: bool) -> i32 { let r = loop { "
                   "if (c) { break { abort(); 1u8 }; } "
                   "let v = 42; break v; }; return r; } " +
                   Main("")));
  EXPECT_TRUE(Runs("fn f(_ c: bool) -> i32 { let r = loop { "
                   "if (c) { break panic() as u8; } "
                   "let v = 42; break v; }; return r; } " +
                   Main("")));
  EXPECT_TRUE(Runs("fn take(_ x: i32) -> u8 { return 0; } "
                   "fn f(_ c: bool) -> i32 { let r = loop { "
                   "if (c) { break take(panic()); } "
                   "let v = 42; break v; }; return r; } " +
                   Main("")));
  EXPECT_FALSE(Accepts("fn f(_ c: bool) -> i32 { let r = loop { "
                       "if (c) { break { unknown(); panic() }; } "
                       "break 42; }; return r; } " +
                       Main("")));
  EXPECT_FALSE(Accepts("fn f(_ c: bool) -> i32 { let r = loop { "
                       "if (c) { break { panic(); true + 1 }; } "
                       "break 42; }; return r; } " +
                       Main("")));
}
TEST(aggregate, block_hints_include_field_destructuring) {
  const std::string prefix = "struct S { x: i32 } ";
  EXPECT_TRUE(Runs(prefix +
                   "fn f(_ c: bool) -> i32 { let x = false; "
                   "let r = if (c) { let S { x } = S { x: 42 }; "
                   "x } else { 42 }; return r; } " +
                   Main("")));
  EXPECT_TRUE(Runs(prefix +
                   "fn f(_ c: bool) -> i32 { let x = false; "
                   "let r = match (c) { true => { "
                   "let S { x } = S { x: 42 }; x }, "
                   "false => 42 }; return r; } " +
                   Main("")));
  EXPECT_TRUE(Runs("enum E { V { field: (bool, i32, bool) } } "
                   "fn f(_ c: bool) -> i32 { let x = false; "
                   "let r = if (c) { "
                   "let E::V { field: (.., x, _) } = "
                   "E::V { field: (true, 42, false) }; "
                   "x } else { 42 }; return r; } " +
                   Main("")));
  EXPECT_TRUE(Runs("struct Pair { flag: bool, count: i32 } "
                   "fn f(_ c: bool) -> i32 { let count = false; "
                   "let r = if (c) { let Pair { count, flag: _ } = "
                   "Pair { flag: true, count: 42 }; count } else { 42 }; "
                   "return r; } " +
                   Main("")));
}
TEST(aggregate, nested_field_rest_stays_in_its_pattern) {
  const std::string prefix =
      "struct S { x: i32, y: bool } struct O { a: S, b: i32 } ";
  EXPECT_TRUE(Runs(prefix + Main("let (S { x, .. },) = "
                                 "(S { x: 42, y: false },); return x;")));
  EXPECT_TRUE(Runs(prefix + Main("let (S { x, .. }, .., n) = "
                                 "(S { x: 40, y: false }, true, 2); "
                                 "return x + n;")));
  EXPECT_TRUE(Runs(prefix + Main("let (S { x: a, .. }, S { x: b, .. }) = "
                                 "(S { x: 40, y: false }, "
                                 "S { x: 2, y: true }); return a + b;")));
  EXPECT_TRUE(Runs(prefix +
                   "fn f(_ c: bool) -> i32 { let x = false; "
                   "let r = if (c) { let O { a: S { x, .. }, .. } = "
                   "O { a: S { x: 42, y: false }, b: 0 }; "
                   "x } else { 42 }; return r; } " +
                   Main("")));
  EXPECT_TRUE(Runs("enum E { V { x: i32, y: bool } } " +
                   Main("let (E::V { x, .. },) = "
                        "(E::V { x: 42, y: false },); return x;")));
  EXPECT_FALSE(Accepts(prefix + Main("let O { a: S { x, .. } } = "
                                     "O { a: S { x: 42, y: false }, b: 0 };")));
  EXPECT_FALSE(Accepts(prefix + Main("let (S { x, .. },) = ();")));
  EXPECT_FALSE(Accepts(prefix + Main("let (S { x, .. },) = (42,);")));
}
TEST(aggregate, per_iteration_storage_and_condition_jumps) {
  EXPECT_TRUE(Runs("struct T {} fn f(_ condition: bool) { while (condition) { "
                   "let local = T {}; let moved = local; continue; } } " +
                   Main("")));
  EXPECT_TRUE(Runs(Main("while ({ break; true }) {}")));
  EXPECT_TRUE(Runs(Main("while ({ continue; true }) {}")));
  EXPECT_TRUE(Runs(Main("while (false) { let never_evaluated = 1 / 0; }")));
  EXPECT_TRUE(
      Runs("fn f() -> i32 { while ({ abort(); true }) {} } " + Main("")));
}
TEST(aggregate, numeric_recovery_loop_edges) {
  EXPECT_TRUE(Runs("fn f(_ n: i32) -> u8 { return loop { "
                   "let x = n as u8 else { break 0u8; }; break x; }; } " +
                   Main("")));
  EXPECT_TRUE(Runs("fn f(_ n: i32) -> u8 { let x = n as u8 else { 0 }; "
                   "return x; } " +
                   Main("")));
  EXPECT_FALSE(Accepts(Main("let x = 127i8 + 1i8 else { 0 };")));
}
TEST(aggregate, independent_layout_table) {
  Types::Builder builder;
  const auto product = builder.declare(Types::Kind::Struct, "LayoutExample");
  builder.define(product, {{"byte", Types::TypeId::U8},
                           {"wide", Types::TypeId::I64},
                           {"tail", Types::TypeId::U16}});
  std::vector<Types::Error> errors;
  const auto types = builder.finish(errors);
  ASSERT_TRUE(types);
  std::string error;
  const auto layouts = Target::Layouts::Build(*types, error);
  ASSERT_TRUE(layouts) << error;
  const auto shape = layouts->get(product);
  EXPECT_EQ(shape.Fields, (std::vector<std::uint64_t>{0, 8, 16}));
  EXPECT_EQ(shape.Alignment, 8U);
  EXPECT_EQ(shape.Size, 24U);
  EXPECT_EQ(Target::EnumTagBits(0), 0U);
  EXPECT_EQ(Target::EnumTagBits(256), 8U);
  EXPECT_EQ(Target::EnumTagBits(257), 16U);
  EXPECT_EQ(Target::EnumTagBits(65536), 16U);
  EXPECT_EQ(Target::EnumTagBits(65537), 32U);
  EXPECT_EQ(Target::EnumTagBits(std::uint64_t{1} << 32), 32U);
  EXPECT_EQ(Target::EnumTagBits((std::uint64_t{1} << 32) + 1), 64U);
  Types::Builder large;
  large.array(Types::TypeId::I64, std::uint64_t{1} << 62);
  errors.clear();
  const auto largeTypes = large.finish(errors);
  ASSERT_TRUE(largeTypes);
  EXPECT_FALSE(Target::Layouts::Build(*largeTypes, error));
}
TEST(aggregate, r2_behavior_and_argument_mapping) {
  EXPECT_TRUE(Runs("fn f(x: i32, _ y: i32, label z: i32) -> i32 { "
                   "return x + y + z; } " +
                   Main("f(label: 10, 20, x: 30); f(x: 10, 20, 30);")));
  EXPECT_FALSE(Accepts("fn f(x: i32, _ y: i32, label z: i32) {} " +
                       Main("f(label: 10, 20, 30);")));
  EXPECT_TRUE(Runs(Main("let n = 300 as u8 else { 0 }; "
                        "if (false && ((1 / 0) == 1)) { abort(); }")));
  EXPECT_TRUE(Runs("fn f(x: u8, _ y: bool, last z: i32) {} " +
                   Main("f(last: 1, true, x: 255);")));
  EXPECT_FALSE(Accepts("fn f(x: i32, _ y: i32, label z: i32) {} " +
                       Main("f(z: 1, 2, x: 3);")));
  EXPECT_FALSE(Accepts("fn f(x: i32, _ y: i32, label y: i32) {} " + Main("")));
}
TEST(aggregate, postponed_features_remain_rejected) {
  for (const auto *source : {"struct T {} impl core::Drop for T {}",
                             "fn f(_ x: &i32) {}", "fn f<T>(_ x: T) {}"}) {
    EXPECT_FALSE(Accepts(std::string(source) + Main("")));
  }
  std::string excessiveType = "i32";
  for (unsigned dimension = 0; dimension < 257; ++dimension) {
    excessiveType += "[0]";
  }
  EXPECT_FALSE(Accepts("alias TooDeep = " + excessiveType + "; " + Main("")));
}
TEST(aggregate, length_evaluates_base_and_sparse_scaling) {
  EXPECT_TRUE(Runs("fn make() -> i32[2] { return [1, 2]; } " +
                   Main("let n = make().len;")));
  std::optional<std::size_t> places;
  for (const auto length : {1ULL, 2ULL, 4ULL, 18446744073709551615ULL}) {
    Diagnostic::Diagnostics diagnostics;
    const auto checked =
        Executable(Main("let a: ()[" + std::to_string(length) + "] = [(); " +
                        std::to_string(length) + "]; let n = a.len;"),
                   diagnostics);
    ASSERT_TRUE(checked);
    const auto count = checked->storage().Functions.back().Places.size();
    if (places) {
      EXPECT_EQ(count, *places);
    }
    places = count;
  }
}
TEST(aggregate, analysis_limits_and_no_fake_normal_arm) {
  Diagnostic::Diagnostics diagnostics;
  EXPECT_FALSE(Executable(Main("let a = 1;"), diagnostics, {0, 100, 100}));
  EXPECT_TRUE(Runs("fn f(_ b: bool) -> i32 { match (b) { "
                   "true => abort(), true => (), false => abort(), }; } " +
                   Main("")));
  EXPECT_TRUE(Runs("fn f(_ b: bool) { match (b) { "
                   "_ if (false) => { let x = 1 / 0; }, _ => (), }; } " +
                   Main("")));
}
} // namespace Chtholly::Testing
