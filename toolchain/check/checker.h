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
          Diagnostic::Diagnostics &diagnostics, Numeric::Limits limits,
          PatternLimits patternLimits);
  auto run() -> std::optional<SemIR::Program>;

private:
  using Id = SemIR::Id;
  using Type = Types::TypeId;
  auto name(Source::Span span) const -> std::string;
  auto type(Parse::TypeSyntax syntax, Type defaultType = Type::Unit) -> Type;
  auto fail(Id node, std::string message) -> void;
  auto error(std::size_t offset, std::string message) -> void;
  auto expect(Id node, Type expected) -> void;
  auto hint(Id node, bool locateOnly = false) -> Type;
  auto hint(Id node, const std::unordered_map<std::string, Type> &locals,
            bool applyDefaults, bool locateOnly = false) -> Type;
  auto hintBindings(Parse::PatternId pattern, Type input,
                    std::unordered_map<std::string, Type> &locals) -> void;
  auto hintBreaks(Id node, const std::string &label, unsigned nestedLoops,
                  std::unordered_map<std::string, Type> locals) -> Type;
  auto mathematical(Id node) const -> bool;
  auto root(Id node) const -> Id;
  auto hasDynamicProjection(Id node) const -> bool;
  auto expression(Id node, Type context, bool executable,
                  bool locateOnly = false) -> Type;
  auto literal(Id node, Type context, bool negative,
               SemIR::Node *destination = nullptr) -> Type;
  auto unary(Id node, Type context, bool executable) -> Type;
  auto binary(Id node, Type context, bool executable) -> Type;
  auto conversion(Id node, bool executable) -> Type;
  auto call(Id node, bool executable) -> Type;
  auto block(Id node, Type context, bool executable,
             bool newScope = true) -> Type;
  auto branch(Id node, Type context, bool executable) -> Type;
  auto statement(Id node, bool executable) -> void;
  auto bind(Id node, bool executable) -> void;
  auto declarations() -> void;
  auto resolveAlias(Id declaration) -> Type;
  auto constant(Id declaration) -> std::optional<Numeric::Value>;
  auto pure(Id node, Type context,
            bool executable) -> std::optional<Numeric::Value>;
  auto length(Id node) -> std::optional<std::uint64_t>;
  auto describe(Type type) const -> std::string;
  auto aggregate(Id node, Type context, bool executable) -> Type;
  auto projection(Id node, bool executable) -> Type;
  auto loop(Id node, Type context, bool executable) -> Type;
  auto match(Id node, Type context, bool executable) -> Type;
  auto pattern(Parse::PatternId syntax, Type type, bool writable, bool trial,
               std::unordered_map<std::string, Id> &bindings) -> Id;
  auto bindPattern(Id pattern, bool trial) -> void;
  auto coverage(Type type, const std::vector<Id> &patterns, Id node,
                bool diagnose = true, Id candidate = SemIR::None) -> bool;
  auto finish(Id node, Numeric::Result result, bool reportEvaluation) -> void;
  auto lookup(const std::string &spelling) const -> std::optional<Id>;
  auto operation(Lex::TokenKind kind,
                 bool unary = false) const -> Numeric::Operation;
  const Source::Buffer &Source;
  const Parse::Tree &Tree;
  Diagnostic::Diagnostics &Diagnostics;
  Numeric::Budget Budget;
  Types::Builder TypeBuilder;
  SemIR::Storage Result;
  std::unordered_map<std::string, Id> Functions;
  std::unordered_map<std::string, Type> TypeNames;
  std::unordered_map<std::string, Id> Aliases;
  std::unordered_map<std::string, Id> Constants;
  std::vector<unsigned char> AliasState;
  std::vector<unsigned char> ConstantState;
  std::vector<std::optional<Numeric::Value>> ConstantValues;
  struct PureLocal {
    Type ValueType;
    std::optional<Numeric::Value> Value;
  };
  std::vector<std::unordered_map<std::string, PureLocal>> PureScopes;
  std::unordered_map<Id, SemIR::Node> PureNodes;
  struct LoopContext {
    Id Node;
    std::string Label;
    bool While;
    Type Result;
    bool HasBreak = false;
  };
  std::vector<LoopContext> Loops;
  std::vector<std::unordered_map<std::string, Id>> Scopes;
  Id CurrentFunction = SemIR::None;
  Id RecoveryRoot = SemIR::None;
  bool Failed = false;
  unsigned ExpressionDepth = 0;
  bool MathContext = false;
  PatternLimits CoverageLimits;
  unsigned DeclarationDepth = 0;
};
} // namespace Chtholly::Check
