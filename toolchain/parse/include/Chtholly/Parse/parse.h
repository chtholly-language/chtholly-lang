// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/Lex/token.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace Chtholly::Diagnostic {
class Diagnostics;
}

namespace Chtholly::Parse {

using NodeId = std::uint32_t;
inline constexpr NodeId NoNode = std::numeric_limits<NodeId>::max();
struct TypeSyntax {
  Source::Span Text{};
  bool Present = false;
  bool Unit = false;
};

enum class Kind {
  Integer,
  Float,
  Boolean,
  Unit,
  Name,
  Group,
  Unary,
  Binary,
  Convert,
  Call,
  Block,
  If,
  Bind,
  Assign,
  Discard,
  Return
};
struct Argument {
  Source::Span Label{};
  bool Named = false;
  NodeId Expression = NoNode;
};
struct Node {
  Kind Form;
  Source::Span Text{};
  Lex::TokenKind Operator = Lex::TokenKind::EndOfFile;
  NodeId Left = NoNode;
  NodeId Right = NoNode;
  NodeId Alternative = NoNode;
  TypeSyntax Annotation;
  bool Mutable = false;
  bool Boolean = false;
  unsigned Radix = 10;
  std::size_t SuffixLength = 0;
  std::vector<NodeId> Statements;
  std::vector<Argument> Arguments;
};
struct Parameter {
  Source::Span Label{};
  Source::Span Name{};
  TypeSyntax Annotation;
  bool Mutable = false;
};
struct Function {
  Source::Span Name{};
  TypeSyntax ReturnType;
  std::vector<Parameter> Parameters;
  NodeId Body = NoNode;
};
struct Tree {
  std::vector<Node> Nodes;
  std::vector<Function> Functions;
};

// Tokens must come from Lex on one still-live Source::Buffer. Returned spans
// refer to that same buffer; parsing never resolves names or numeric values.
auto Parse(std::span<const Lex::Token> tokens,
           Diagnostic::Diagnostics &diagnostics) -> std::optional<Tree>;

} // namespace Chtholly::Parse
