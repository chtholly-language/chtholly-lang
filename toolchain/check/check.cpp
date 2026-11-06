// SPDX-License-Identifier: MIT OR Apache-2.0
#include "checker.h"
#include <Chtholly/Target/layout.h>
#include <stdexcept>
#include <unordered_set>

namespace Chtholly::Check {
using Type = Types::TypeId;
using Id = SemIR::Id;
Checker::Checker(const Source::Buffer &source, const Parse::Tree &tree,
                 Diagnostic::Diagnostics &diagnostics, Numeric::Limits limits,
                 PatternLimits patternLimits)
    : Source(source), Tree(tree), Diagnostics(diagnostics), Budget(limits) {
  Result.SourceName = source.name();
  Result.Nodes.resize(tree.Nodes.size());
  for (std::size_t i = 0; i < tree.Nodes.size(); ++i) {
    Result.Nodes[i].Span = tree.Nodes[i].Text;
    Result.Nodes[i].Location = source.locate(tree.Nodes[i].Text.Begin);
  }
  CoverageLimits = patternLimits;
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
  if (syntax.Form == Parse::TypeSyntax::Kind::Tuple) {
    std::vector<Type> elements;
    for (const auto &element : syntax.Elements) {
      elements.push_back(type(element));
    }
    return TypeBuilder.tuple(std::move(elements));
  }
  if (syntax.Form == Parse::TypeSyntax::Kind::Array) {
    const auto element = type(syntax.Elements.at(0));
    const auto count = length(syntax.Length);
    return count ? TypeBuilder.array(element, *count) : Type::Invalid;
  }
  const auto spelling = name(syntax.Text);
  const auto resolved = Types::Lookup(spelling);
  if (resolved) {
    return Type(*resolved);
  }
  if (const auto found = TypeNames.find(spelling); found != TypeNames.end()) {
    return found->second;
  }
  if (const auto alias = Aliases.find(spelling); alias != Aliases.end()) {
    return resolveAlias(alias->second);
  }
  error(syntax.Text.Begin, "unknown or unsupported type '" + spelling + "'");
  return Type::Invalid;
}
auto Checker::describe(Type valueType) const -> std::string {
  return valueType.owner() == nullptr
             ? std::string(Types::Name(valueType.primitive()))
             : TypeBuilder.get(valueType).Name;
}
auto Checker::expect(Id node, Type expected) -> void {
  const auto actual = Result.Nodes[node].ValueType;
  if (!Result.Nodes[node].Completes) {
    return;
  }
  if (expected != Type::Invalid && actual != Type::Invalid &&
      actual != Type::Never && actual != expected) {
    fail(node, "expected " + describe(expected) + ", got " + describe(actual));
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
auto Checker::hasDynamicProjection(Id node) const -> bool {
  while (node != SemIR::None) {
    const auto &record = Result.Nodes[node];
    if (record.Form == SemIR::Kind::Index && !record.FixedIndex) {
      return true;
    }
    if (record.Form != SemIR::Kind::Group &&
        record.Form != SemIR::Kind::Project &&
        record.Form != SemIR::Kind::Index) {
      return false;
    }
    node = record.Left;
  }
  return false;
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
auto Checker::hint(Id node, bool locateOnly) -> Type {
  return hint(node, {}, false, locateOnly);
}
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
  declarations();
  if (Failed) {
    return std::nullopt;
  }
  for (const auto &syntax : Tree.Functions) {
    SemIR::Function function;
    function.Name = name(syntax.Name);
    function.ReturnType = type(syntax.ReturnType);
    function.Body = syntax.Body;
    const auto index = static_cast<Id>(Result.Functions.size());
    if (function.Name == "_" || TypeNames.contains(function.Name) ||
        Constants.contains(function.Name) ||
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
      function.HasNormalEntry &= parameterType != Type::Invalid &&
                                 TypeBuilder.inhabited(parameterType);
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
  std::vector<Types::Error> typeErrors;
  Result.Types = TypeBuilder.finish(typeErrors);
  for (const auto &failure : typeErrors) {
    error(0, failure.Message);
  }
  if (Result.Types) {
    std::string layoutError;
    if (!Target::Layouts::Build(*Result.Types, layoutError, Result.Target)) {
      error(0, "target layout: " + layoutError);
    }
  }
  if (Failed || !Result.Types) {
    return std::nullopt;
  }
  return SemIR::Program(std::move(Result));
}
auto Check(const Source::Buffer &source, const Parse::Tree &tree,
           Diagnostic::Diagnostics &diagnostics, Numeric::Limits limits,
           PatternLimits patternLimits) -> std::optional<SemIR::Program> {
  try {
    return Checker(source, tree, diagnostics, limits, patternLimits).run();
  } catch (const std::logic_error &failure) {
    diagnostics.error("check", failure.what());
    return std::nullopt;
  }
}
} // namespace Chtholly::Check
