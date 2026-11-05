// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/Check/check.h>
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/Parse/parse.h>
#include <unordered_map>

namespace Chtholly::Check {
class Checker {
public:
  Checker(const Source::Buffer &source, const Parse::Tree &tree,
          Diagnostic::Diagnostics &diagnostics, Numeric::Limits limits);
  auto run() -> std::optional<SemIR::Program>;

private:
  using Id = SemIR::Id;
  using Type = Target::Type;
  auto name(Source::Span span) const -> std::string;
  auto type(Parse::TypeSyntax syntax, Type defaultType = Type::Unit) -> Type;
  auto fail(Id node, std::string message) -> void;
  auto error(std::size_t offset, std::string message) -> void;
  auto expect(Id node, Type expected) -> void;
  auto hint(Id node) const -> Type;
  auto hint(Id node, const std::unordered_map<std::string, Type> &locals,
            bool applyDefaults) const -> Type;
  auto mathematical(Id node) const -> bool;
  auto root(Id node) const -> Id;
  auto expression(Id node, Type context, bool executable) -> Type;
  auto literal(Id node, Type context, bool negative) -> Type;
  auto unary(Id node, Type context, bool executable) -> Type;
  auto binary(Id node, Type context, bool executable) -> Type;
  auto conversion(Id node, bool executable) -> Type;
  auto call(Id node, bool executable) -> Type;
  auto block(Id node, Type context, bool executable,
             bool newScope = true) -> Type;
  auto branch(Id node, Type context, bool executable) -> Type;
  auto statement(Id node, bool executable) -> void;
  auto bind(Id node, bool executable) -> void;
  auto finish(Id node, Numeric::Result result, bool reportEvaluation) -> void;
  auto lookup(const std::string &spelling) const -> std::optional<Id>;
  auto operation(Lex::TokenKind kind,
                 bool unary = false) const -> Numeric::Operation;
  const Source::Buffer &Source;
  const Parse::Tree &Tree;
  Diagnostic::Diagnostics &Diagnostics;
  Numeric::Budget Budget;
  SemIR::Storage Result;
  std::unordered_map<std::string, Id> Functions;
  std::vector<std::unordered_map<std::string, Id>> Scopes;
  Id CurrentFunction = SemIR::None;
  Id RecoveryRoot = SemIR::None;
  bool Failed = false;
  unsigned ExpressionDepth = 0;
};
} // namespace Chtholly::Check
