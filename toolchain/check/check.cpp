// SPDX-License-Identifier: MIT OR Apache-2.0
#include "checker.h"
#include <unordered_set>

namespace Chtholly::Check {
using Type = Target::Type;
using Id = SemIR::Id;
Checker::Checker(const Source::Buffer &source, const Parse::Tree &tree,
                 Diagnostic::Diagnostics &diagnostics, Numeric::Limits limits)
    : Source(source), Tree(tree), Diagnostics(diagnostics), Budget(limits) {
  Result.SourceName = source.name();
  Result.Nodes.resize(tree.Nodes.size());
  for (std::size_t i = 0; i < tree.Nodes.size(); ++i) {
    Result.Nodes[i].Span = tree.Nodes[i].Text;
    Result.Nodes[i].Location = source.locate(tree.Nodes[i].Text.Begin);
  }
}
auto Checker::name(Source::Span span) const -> std::string {
  auto spelling = Source.slice(span);
  if (spelling.size() >= 2 && spelling.front() == '`') {
    spelling.remove_prefix(1);
    spelling.remove_suffix(1);
  }
  return std::string(spelling);
}
auto Checker::error(std::size_t offset, std::string message) -> void {
  Diagnostics.sourceError(offset, std::move(message));
  Failed = true;
}
auto Checker::fail(Id node, std::string message) -> void {
  error(Tree.Nodes.at(node).Text.Begin, std::move(message));
}
auto Checker::type(Parse::TypeSyntax syntax, Type defaultType) -> Type {
  if (!syntax.Present) {
    return defaultType;
  }
  if (syntax.Unit) {
    return Type::Unit;
  }
  const auto spelling = name(syntax.Text);
  const auto resolved = Target::Lookup(spelling);
  if (!resolved) {
    error(syntax.Text.Begin, "unknown or unsupported type '" + spelling + "'");
    return Type::Invalid;
  }
  return *resolved;
}
auto Checker::expect(Id node, Type expected) -> void {
  const auto actual = Result.Nodes[node].ValueType;
  if (!Result.Nodes[node].Completes) {
    return;
  }
  if (expected != Type::Invalid && actual != Type::Invalid &&
      actual != Type::Never && actual != expected) {
    fail(node, "expected " + std::string(Target::Name(expected)) + ", got " +
                   std::string(Target::Name(actual)));
  }
}
auto Checker::lookup(const std::string &spelling) const -> std::optional<Id> {
  for (auto scope = Scopes.rbegin(); scope != Scopes.rend(); ++scope) {
    const auto found = scope->find(spelling);
    if (found != scope->end()) {
      return found->second;
    }
  }
  return std::nullopt;
}
auto Checker::root(Id node) const -> Id {
  while (Tree.Nodes[node].Form == Parse::Kind::Group) {
    node = Tree.Nodes[node].Left;
  }
  return node;
}
auto Checker::mathematical(Id node) const -> bool {
  const auto &value = Tree.Nodes[node];
  switch (value.Form) {
  case Parse::Kind::Integer:
    return value.SuffixLength == 0;
  case Parse::Kind::Group:
    return mathematical(value.Left);
  case Parse::Kind::Unary:
    return (value.Operator == Lex::TokenKind::Plus ||
            value.Operator == Lex::TokenKind::Minus) &&
           mathematical(value.Left);
  case Parse::Kind::Binary:
    return (value.Operator == Lex::TokenKind::Plus ||
            value.Operator == Lex::TokenKind::Minus ||
            value.Operator == Lex::TokenKind::Star ||
            value.Operator == Lex::TokenKind::Slash ||
            value.Operator == Lex::TokenKind::Percent) &&
           mathematical(value.Left) && mathematical(value.Right);
  default:
    return false;
  }
}
auto Checker::hint(Id node) const -> Type { return hint(node, {}, false); }
auto Checker::operation(Lex::TokenKind token,
                        bool unary) const -> Numeric::Operation {
  using K = Lex::TokenKind;
  using O = Numeric::Operation;
  switch (token) {
  case K::Plus:
    return unary ? O::Plus : O::Add;
  case K::Minus:
    return unary ? O::Negate : O::Subtract;
  case K::Star:
    return O::Multiply;
  case K::Slash:
    return O::Divide;
  case K::Percent:
    return O::Remainder;
  case K::Bang:
    return O::Not;
  case K::Tilde:
    return O::Complement;
  case K::Amp:
    return O::BitAnd;
  case K::Pipe:
    return O::BitOr;
  case K::Caret:
    return O::BitXor;
  case K::ShiftLeft:
    return O::ShiftLeft;
  case K::ShiftRight:
    return O::ShiftRight;
  case K::Less:
    return O::Less;
  case K::LessEqual:
    return O::LessEqual;
  case K::Greater:
    return O::Greater;
  case K::GreaterEqual:
    return O::GreaterEqual;
  case K::Equal:
    return O::Equal;
  case K::NotEqual:
    return O::NotEqual;
  case K::And:
    return O::LogicalAnd;
  case K::Or:
    return O::LogicalOr;
  default:
    return O::Plus;
  }
}
auto Checker::run() -> std::optional<SemIR::Program> {
  for (const auto &syntax : Tree.Functions) {
    SemIR::Function function;
    function.Name = name(syntax.Name);
    function.ReturnType = type(syntax.ReturnType);
    function.Body = syntax.Body;
    const auto index = static_cast<Id>(Result.Functions.size());
    if (function.Name == "_" ||
        !Functions.emplace(function.Name, index).second) {
      error(syntax.Name.Begin, "duplicate or invalid function name");
    }
    std::unordered_set<std::string> labels;
    for (const auto &parameter : syntax.Parameters) {
      const auto parameterType = type(parameter.Annotation);
      const auto label = name(parameter.Label);
      if (label != "_" && !labels.insert(label).second) {
        error(parameter.Label.Begin, "duplicate parameter label");
      }
      const auto local = static_cast<Id>(function.Locals.size());
      function.Parameters.push_back(
          {label == "_" ? "" : label, local, parameterType});
      function.Locals.push_back(
          {name(parameter.Name), parameterType, parameter.Mutable});
      function.HasNormalEntry &= parameterType != Type::Never;
    }
    if (function.Name == "main") {
      Result.Entry = index;
      if (function.ReturnType != Type::I32 || !function.Parameters.empty()) {
        error(syntax.Name.Begin, "main must have no parameters and return i32");
      }
    }
    Result.Functions.push_back(std::move(function));
  }
  if (Result.Entry == SemIR::None) {
    error(0, "program requires a unique main function");
  }
  for (Id i = 0; i < Result.Functions.size(); ++i) {
    CurrentFunction = i;
    Scopes.clear();
    Scopes.emplace_back();
    auto &function = Result.Functions[i];
    for (const auto &parameter : function.Parameters) {
      const auto &local = function.Locals[parameter.Local];
      if (local.Name != "_" &&
          !Scopes.back().emplace(local.Name, parameter.Local).second) {
        error(Tree.Functions[i].Name.Begin, "duplicate parameter local name");
      }
    }
    block(function.Body, Type::Invalid, function.HasNormalEntry, false);
    const auto &body = Result.Nodes[function.Body];
    if (function.HasNormalEntry && body.Completes) {
      if (function.ReturnType != Type::Unit) {
        fail(function.Body,
             "normal function exit requires an explicit return value");
      } else {
        expect(function.Body, Type::Unit);
      }
    }
  }
  if (Failed) {
    return std::nullopt;
  }
  return SemIR::Program(std::move(Result));
}
auto Check(const Source::Buffer &source, const Parse::Tree &tree,
           Diagnostic::Diagnostics &diagnostics,
           Numeric::Limits limits) -> std::optional<SemIR::Program> {
  return Checker(source, tree, diagnostics, limits).run();
}
} // namespace Chtholly::Check
