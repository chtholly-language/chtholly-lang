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
  enum class Kind { Name, Tuple, Array } Form = Kind::Name;
  std::vector<TypeSyntax> Elements;
  NodeId Length = NoNode;
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
  Return,
  Tuple,
  Array,
  Repeat,
  Construct,
  Project,
  Index,
  Copy,
  Move,
  Match,
  While,
  Loop,
  Break,
  Continue
};
using PatternId = std::uint32_t;
inline constexpr PatternId NoPattern = std::numeric_limits<PatternId>::max();
enum class PatternKind {
  Binding,
  Wildcard,
  Unit,
  Tuple,
  Struct,
  Variant,
  Boolean,
  Integer,
  Or,
  Group
};
struct Pattern {
  PatternKind Form = PatternKind::Wildcard;
  Source::Span Text{};
  Source::Span Variant{};
  bool Mutable = false;
  bool Rest = false;
  bool Negative = false;
  bool Boolean = false;
  unsigned Radix = 10;
  std::size_t SuffixLength = 0;
  std::vector<PatternId> Children;
  std::vector<Source::Span> Fields;

  // A field pattern can itself contain '..'. Only a standalone token skips
  // positions in an enclosing tuple; nested field elision stays in its owner.
  auto isTupleRest() const -> bool {
    return Form == PatternKind::Wildcard && Rest;
  }
};
struct Arm {
  PatternId Pattern = NoPattern;
  NodeId Guard = NoNode;
  NodeId Body = NoNode;
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
  std::vector<Source::Span> Fields;
  Source::Span Member{};
  Source::Span Label{};
  PatternId Pattern = NoPattern;
  std::vector<Arm> Arms;
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
struct Field {
  Source::Span Name;
  TypeSyntax Type;
};
struct Variant {
  Source::Span Name;
  std::vector<Field> Fields;
};
struct Declaration {
  enum class Kind { Struct, Enum, Alias, Const, CopyImpl } Form;
  Source::Span Name{};
  Source::Span Member{};
  TypeSyntax Type;
  NodeId Initializer = NoNode;
  std::vector<Field> Fields;
  std::vector<Variant> Variants;
};
struct Tree {
  std::vector<Node> Nodes;
  std::vector<Function> Functions;
  std::vector<Declaration> Declarations;
  std::vector<Pattern> Patterns;
};

// Tokens must come from Lex on one still-live Source::Buffer. Returned spans
// refer to that same buffer; parsing never resolves names or numeric values.
auto Parse(std::span<const Lex::Token> tokens,
           Diagnostic::Diagnostics &diagnostics) -> std::optional<Tree>;

} // namespace Chtholly::Parse
