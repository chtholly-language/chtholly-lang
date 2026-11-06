// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Testing/frontend.h>
#include <array>
#include <gtest/gtest.h>

namespace Chtholly::Testing {
namespace {
// Test-side two-slot model. No production transfer or join routine is used.
enum class Action { MoveA, MoveB, RestoreA, RestoreB, Return };
using Trace = std::vector<Action>;
struct Model {
  bool Normal = true;
  bool A = true;
  bool B = true;
  auto operator==(const Model &) const -> bool = default;
};
auto Join(Model left, Model right) -> Model {
  if (!left.Normal) {
    return right;
  }
  if (!right.Normal) {
    return left;
  }
  return {true, left.A && right.A, left.B && right.B};
}
auto Interpret(Model state, const Trace &trace) -> std::optional<Model> {
  for (const auto action : trace) {
    if (!state.Normal) {
      break;
    }
    switch (action) {
    case Action::MoveA:
      if (!state.A) {
        return std::nullopt;
      }
      state.A = false;
      break;
    case Action::MoveB:
      if (!state.B) {
        return std::nullopt;
      }
      state.B = false;
      break;
    case Action::RestoreA:
      state.A = true;
      break;
    case Action::RestoreB:
      state.B = true;
      break;
    case Action::Return:
      state.Normal = false;
      break;
    }
  }
  return state;
}
auto SourceTrace(const Trace &trace, std::string_view prefix) -> std::string {
  std::string result;
  for (std::size_t i = 0; i < trace.size(); ++i) {
    switch (trace[i]) {
    case Action::MoveA:
      result +=
          "let " + std::string(prefix) + std::to_string(i) + " = move pair.a; ";
      break;
    case Action::MoveB:
      result +=
          "let " + std::string(prefix) + std::to_string(i) + " = move pair.b; ";
      break;
    case Action::RestoreA:
      result += "pair.a = Token {}; ";
      break;
    case Action::RestoreB:
      result += "pair.b = Token {}; ";
      break;
    case Action::Return:
      result += "return; ";
      break;
    }
  }
  return result;
}
auto Program(std::string body) -> std::string {
  return "struct Token {} struct Pair { a: Token, b: Token } "
         "fn probe(_ c: bool) { "
         "var pair = Pair { a: Token {}, b: Token {} }; " +
         body + " let complete = pair; } fn main() -> i32 { return 0; }";
}
auto Accepted(const std::string &source) -> bool {
  Diagnostic::Diagnostics diagnostics;
  return Executable(source, diagnostics).has_value();
}
auto Traces() -> std::vector<Trace> {
  std::vector<Trace> result{{}};
  constexpr std::array Actions = {Action::MoveA, Action::MoveB,
                                  Action::RestoreA, Action::RestoreB,
                                  Action::Return};
  for (const auto first : Actions) {
    result.push_back({first});
    for (const auto second : Actions) {
      result.push_back({first, second});
    }
  }
  return result;
}
} // namespace
TEST(flow, independent_short_traces) {
  const auto traces = Traces();
  for (const auto &first : traces) {
    for (const auto &second : traces) {
      const auto a = Interpret({}, first);
      const auto b = Interpret({}, second);
      bool expected = false;
      if (a && b) {
        const auto join = Join(*a, *b);
        expected = !join.Normal || (join.A && join.B);
      }
      const auto source =
          Program("if (c) { " + SourceTrace(first, "left") + " } else { " +
                  SourceTrace(second, "right") + " }");
      EXPECT_EQ(Accepted(source), expected) << source;
    }
  }
}
TEST(flow, join_and_loop_fixed_points) {
  const std::array States = {Model{false, true, true}, Model{true, true, true},
                             Model{true, false, true}, Model{true, true, false},
                             Model{true, false, false}};
  for (const auto a : States) {
    EXPECT_EQ(Join(a, a), a);
    for (const auto b : States) {
      EXPECT_EQ(Join(a, b), Join(b, a));
      for (const auto c : States) {
        EXPECT_EQ(Join(Join(a, b), c), Join(a, Join(b, c)));
      }
    }
  }
  for (const auto &trace : Traces()) {
    Model header;
    bool valid = true;
    for (unsigned iteration = 0; iteration < 4; ++iteration) {
      const auto body = Interpret(header, trace);
      if (!body) {
        valid = false;
        break;
      }
      const auto next = Join(Model{}, *body);
      if (next == header) {
        break;
      }
      header = next;
    }
    const bool expected = valid && header.A && header.B;
    const auto source =
        Program("while (c) { " + SourceTrace(trace, "step") + " }");
    EXPECT_EQ(Accepted(source), expected) << source;
  }
}
} // namespace Chtholly::Testing
