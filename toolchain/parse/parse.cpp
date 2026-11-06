// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/Parse/parse.h>

#include <algorithm>
#include <string>
#include <utility>

namespace Chtholly::Parse {
namespace {
using K = Lex::TokenKind;
auto Precedence(K token) -> int {
  switch (token) {
  case K::Or:
    return 1;
  case K::And:
    return 2;
  case K::Equal:
  case K::NotEqual:
    return 3;
  case K::Less:
  case K::LessEqual:
  case K::Greater:
  case K::GreaterEqual:
    return 4;
  case K::Pipe:
    return 5;
  case K::Caret:
    return 6;
  case K::Amp:
    return 7;
  case K::ShiftLeft:
  case K::ShiftRight:
    return 8;
  case K::Plus:
  case K::Minus:
    return 9;
  case K::Star:
  case K::Slash:
  case K::Percent:
    return 10;
  case K::As:
    return 11;
  default:
    return 0;
  }
}
class Parser {
public:
  Parser(std::span<const Lex::Token> tokens,
         Diagnostic::Diagnostics &diagnostics)
      : Tokens(tokens), Diagnostics(diagnostics) {}
  auto run() -> std::optional<Tree> {
    if (Tokens.empty() || Tokens.back().Kind != K::EndOfFile) {
      fail("token stream must end in EOF");
      return std::nullopt;
    }
    while (!Failed && kind() != K::EndOfFile) {
      take(K::Pub);
      if (kind() != K::Fn && kind() != K::Struct && kind() != K::Enum &&
          kind() != K::Alias && kind() != K::Const && kind() != K::Impl) {
        fail("expected a supported top-level declaration "
             "(fn, struct, enum, alias, const, or impl)");
        break;
      }
      if (kind() != K::Fn) {
        declaration();
        continue;
      }
      Function function;
      expect(K::Fn, "'fn' (unsupported declarations are not accepted)");
      function.Name = expect(K::Identifier, "a function name").Text;
      expect(K::LeftParen, "'('");
      if (kind() != K::RightParen) {
        do {
          Parameter parameter;
          parameter.Mutable = take(K::Var);
          parameter.Label =
              expect(K::Identifier, "a parameter name or label").Text;
          parameter.Name = parameter.Label;
          if (kind() == K::Identifier) {
            parameter.Name = advance().Text;
          }
          expect(K::Colon, "':' before the parameter type");
          parameter.Annotation = type();
          function.Parameters.push_back(parameter);
        } while (take(K::Comma) && kind() != K::RightParen && !Failed);
      }
      expect(K::RightParen, "')'");
      if (take(K::Arrow)) {
        function.ReturnType = type();
      }
      function.Body = block();
      Result.Functions.push_back(std::move(function));
    }
    if (Cursor + 1 != Tokens.size()) {
      fail("unexpected token after EOF");
    }
    return Failed ? std::nullopt : std::optional<Tree>(std::move(Result));
  }

private:
  auto fields() -> std::vector<Field> {
    std::vector<Field> result;
    expect(K::LeftBrace, "'{' before fields");
    while (!Failed && kind() != K::RightBrace) {
      take(K::Pub);
      Field field;
      field.Name = expect(K::Identifier, "a field name").Text;
      expect(K::Colon, "':' after field name");
      field.Type = type();
      result.push_back(std::move(field));
      if (!take(K::Comma)) {
        break;
      }
    }
    expect(K::RightBrace, "'}' after fields");
    return result;
  }
  auto declaration() -> void {
    const auto keyword = advance();
    Declaration result;
    if (keyword.Kind == K::Impl) {
      result.Form = Declaration::Kind::CopyImpl;
      result.Name =
          expect(K::Identifier, "the canonical core::Copy interface").Text;
      expect(K::Path, "'::' in the interface path");
      const auto member = expect(K::Identifier, "Copy").Text;
      result.Member = member;
      expect(K::For, "'for' in Copy implementation");
      result.Type = type();
      expect(K::LeftBrace, "'{' in empty Copy implementation");
      expect(K::RightBrace, "'}' (Copy has no user implementation body)");
    } else {
      result.Name = expect(K::Identifier, "a declaration name").Text;
      switch (keyword.Kind) {
      case K::Struct:
        result.Form = Declaration::Kind::Struct;
        result.Fields = fields();
        break;
      case K::Enum:
        result.Form = Declaration::Kind::Enum;
        expect(K::LeftBrace, "'{' before enum variants");
        while (!Failed && kind() != K::RightBrace) {
          take(K::Pub);
          Variant variant;
          variant.Name = expect(K::Identifier, "a variant name").Text;
          if (kind() == K::LeftBrace) {
            variant.Fields = fields();
          }
          result.Variants.push_back(std::move(variant));
          if (!take(K::Comma)) {
            break;
          }
        }
        expect(K::RightBrace, "'}' after enum variants");
        break;
      case K::Alias:
        result.Form = Declaration::Kind::Alias;
        expect(K::Assign, "'=' in alias declaration");
        result.Type = type();
        expect(K::Semicolon, "';' after alias");
        break;
      case K::Const:
        result.Form = Declaration::Kind::Const;
        expect(K::Colon, "':' and an explicit const type");
        result.Type = type();
        expect(K::Assign, "'=' in const declaration");
        result.Initializer = expression();
        expect(K::Semicolon, "';' after const initializer");
        break;
      default:
        fail("unsupported declaration in R3");
        return;
      }
    }
    Result.Declarations.push_back(std::move(result));
  }
  auto pattern(bool alternatives = true) -> PatternId {
    if (++PatternDepth > 256) {
      fail("pattern nesting limit exceeded");
      --PatternDepth;
      return NoPattern;
    }
    Pattern result;
    result.Mutable = take(K::Var);
    auto token = advance();
    result.Text = token.Text;
    if (token.Kind == K::Minus) {
      result.Negative = true;
      // Grouping is transparent for the one-negative-literal rule.
      const auto nested = pattern(false);
      if (nested != NoPattern) {
        auto inner = Result.Patterns[nested];
        while (inner.Form == PatternKind::Group && !inner.Children.empty()) {
          inner = Result.Patterns[inner.Children[0]];
        }
        if (inner.Form != PatternKind::Integer || inner.Negative) {
          fail("a negative pattern requires one integer literal");
        }
        result = std::move(inner);
        result.Negative = true;
      }
    } else if (token.Kind == K::Rest) {
      if (result.Mutable) {
        fail("var inside a pattern must mark a leaf binding");
      }
      result.Form = PatternKind::Wildcard;
      result.Rest = true;
    } else if (token.Kind == K::Integer) {
      result.Form = PatternKind::Integer;
      result.Radix = token.Radix;
      result.SuffixLength = token.SuffixLength;
    } else if (token.Kind == K::True || token.Kind == K::False) {
      result.Form = PatternKind::Boolean;
      result.Boolean = token.Kind == K::True;
    } else if (token.Kind == K::LeftParen) {
      result.Form = PatternKind::Unit;
      if (!take(K::RightParen)) {
        result.Children.push_back(pattern());
        result.Form = PatternKind::Group;
        if (take(K::Comma)) {
          result.Form = PatternKind::Tuple;
          while (!Failed && kind() != K::RightParen) {
            result.Children.push_back(pattern());
            if (!take(K::Comma)) {
              break;
            }
          }
        }
        expect(K::RightParen, "')' after pattern");
      }
    } else if (token.Kind == K::Identifier) {
      result.Form = PatternKind::Binding;
      if (take(K::Path)) {
        result.Variant = expect(K::Identifier, "an enum variant").Text;
        result.Form = PatternKind::Variant;
      }
      if (take(K::LeftBrace)) {
        if (result.Form != PatternKind::Variant) {
          result.Form = PatternKind::Struct;
        }
        while (!Failed && kind() != K::RightBrace) {
          if (take(K::Rest)) {
            if (result.Rest) {
              fail("a field pattern can contain at most one '..'");
            }
            result.Rest = true;
            if (take(K::Comma)) {
              continue;
            }
            break;
          }
          const bool mutableShorthand = take(K::Var);
          const auto field = expect(K::Identifier, "a pattern field").Text;
          result.Fields.push_back(field);
          if (take(K::Colon)) {
            if (mutableShorthand) {
              fail("write 'field: var binding' or the shorthand 'var field'");
            }
            result.Children.push_back(pattern());
          } else {
            Pattern binding;
            binding.Form = PatternKind::Binding;
            binding.Text = field;
            binding.Mutable = mutableShorthand;
            result.Children.push_back(
                static_cast<PatternId>(Result.Patterns.size()));
            Result.Patterns.push_back(std::move(binding));
          }
          if (!take(K::Comma)) {
            break;
          }
        }
        expect(K::RightBrace, "'}' after field pattern");
      }
    } else {
      fail("unsupported pattern in R3");
    }
    auto id = static_cast<PatternId>(Result.Patterns.size());
    Result.Patterns.push_back(std::move(result));
    if (alternatives && take(K::Pipe)) {
      Pattern combined;
      combined.Form = PatternKind::Or;
      combined.Text = token.Text;
      combined.Children.push_back(id);
      do {
        combined.Children.push_back(pattern(false));
      } while (!Failed && take(K::Pipe));
      id = static_cast<PatternId>(Result.Patterns.size());
      Result.Patterns.push_back(std::move(combined));
    }
    --PatternDepth;
    return id;
  }
  auto kind(std::size_t lookahead = 0) const -> K {
    return Cursor + lookahead < Tokens.size() ? Tokens[Cursor + lookahead].Kind
                                              : K::EndOfFile;
  }
  auto advance() -> Lex::Token {
    return Cursor < Tokens.size() ? Tokens[Cursor++]
                                  : Lex::Token{K::EndOfFile, {0, 0}};
  }
  auto take(K token) -> bool {
    if (kind() != token) {
      return false;
    }
    advance();
    return true;
  }
  auto fail(std::string message) -> void {
    if (!Failed) {
      Diagnostics.sourceError(Cursor < Tokens.size() ? Tokens[Cursor].Text.Begin
                                                     : 0,
                              std::move(message));
    }
    Failed = true;
  }
  auto expect(K token, const char *description) -> Lex::Token {
    if (kind() != token) {
      fail(std::string("expected ") + description);
      return {token, {0, 0}};
    }
    return advance();
  }
  auto add(Node node) -> NodeId {
    if (Result.Nodes.size() >= NoNode) {
      fail("AST node limit exceeded");
      return NoNode;
    }
    const auto id = static_cast<NodeId>(Result.Nodes.size());
    unsigned depth = 1;
    const auto child = [&](NodeId value) {
      if (value != NoNode) {
        depth = std::max(depth, NodeDepth.at(value) + 1);
      }
    };
    child(node.Left);
    child(node.Right);
    child(node.Alternative);
    for (const auto statement : node.Statements) {
      child(statement);
    }
    for (const auto &argument : node.Arguments) {
      child(argument.Expression);
    }
    for (const auto &arm : node.Arms) {
      child(arm.Guard);
      child(arm.Body);
    }
    if (depth > 256) {
      fail("syntax tree depth limit exceeded");
    }
    NodeDepth.push_back(depth);
    Result.Nodes.push_back(std::move(node));
    return id;
  }
  auto type() -> TypeSyntax {
    if (++TypeDepth > 256) {
      fail("type nesting limit exceeded");
      --TypeDepth;
      return {};
    }
    const auto first =
        Cursor < Tokens.size() ? Tokens[Cursor].Text : Source::Span{};
    TypeSyntax result{first, true};
    if (take(K::Bang) || take(K::Identifier)) {
      // A name is resolved by Check, never by the parser.
    } else if (take(K::LeftParen)) {
      result.Form = TypeSyntax::Kind::Tuple;
      if (take(K::RightParen)) {
        result.Unit = true;
      } else {
        auto firstElement = type();
        if (!take(K::Comma)) {
          expect(K::RightParen, "')' after grouped type");
          result = std::move(firstElement);
        } else {
          result.Elements.push_back(std::move(firstElement));
          while (!Failed && kind() != K::RightParen) {
            result.Elements.push_back(type());
            if (!take(K::Comma)) {
              break;
            }
          }
          expect(K::RightParen, "')' after tuple type");
        }
      }
    } else {
      fail("expected a supported type");
    }
    std::vector<NodeId> dimensions;
    while (!Failed && take(K::LeftBracket)) {
      if (dimensions.size() >= 256) {
        fail("type nesting limit exceeded");
        break;
      }
      dimensions.push_back(expression());
      expect(K::RightBracket, "']' after fixed array length");
    }
    for (auto i = dimensions.rbegin(); i != dimensions.rend(); ++i) {
      TypeSyntax array{first, true};
      array.Form = TypeSyntax::Kind::Array;
      array.Length = *i;
      array.Elements.push_back(std::move(result));
      result = std::move(array);
    }
    // Consecutive array suffixes are parsed by a loop, so the parser's call
    // depth alone does not bound the resulting type tree. Count actual nodes
    // iteratively, including grouped and tuple elements, before publishing it.
    std::vector<std::pair<const TypeSyntax *, unsigned>> pending{{&result, 1}};
    while (!Failed && !pending.empty()) {
      const auto [syntax, depth] = pending.back();
      pending.pop_back();
      if (depth > 256) {
        fail("type nesting limit exceeded");
        break;
      }
      for (const auto &element : syntax->Elements) {
        pending.push_back({&element, depth + 1});
      }
    }
    --TypeDepth;
    return result;
  }
  auto block() -> NodeId {
    if (++BlockDepth > 256) {
      fail("block nesting limit exceeded");
      --BlockDepth;
      return NoNode;
    }
    Node result{Kind::Block};
    result.Text = expect(K::LeftBrace, "'{'").Text;
    while (!Failed && kind() != K::RightBrace && kind() != K::EndOfFile) {
      if (kind() == K::Let || kind() == K::Var) {
        const auto keyword = advance();
        Node binding{Kind::Bind};
        binding.Mutable = keyword.Kind == K::Var;
        binding.Pattern = pattern();
        if (Failed || binding.Pattern == NoPattern) {
          break;
        }
        binding.Text = Result.Patterns[binding.Pattern].Text;
        if (take(K::Colon)) {
          binding.Annotation = type();
        }
        expect(K::Assign, "'=' (locals require an initializer)");
        binding.Left = expression();
        if (take(K::Else)) {
          binding.Alternative = block();
        }
        expect(K::Semicolon, "';' after the binding");
        result.Statements.push_back(add(std::move(binding)));
        continue;
      }
      if (kind() == K::Return || kind() == K::Break || kind() == K::Continue) {
        Node statement{Kind::Return};
        const auto jump = advance();
        statement.Text = jump.Text;
        statement.Form = jump.Kind == K::Return  ? Kind::Return
                         : jump.Kind == K::Break ? Kind::Break
                                                 : Kind::Continue;
        if (jump.Kind != K::Return && take(K::At)) {
          statement.Label =
              expect(K::Identifier, "a loop label after '@'").Text;
        }
        if (kind() != K::Semicolon) {
          statement.Left = expression();
        }
        expect(K::Semicolon, "';' after return");
        result.Statements.push_back(add(std::move(statement)));
        continue;
      }
      const auto value = expression();
      if (Failed) {
        break;
      }
      if (take(K::Assign)) {
        Node assignment{Kind::Assign};
        assignment.Text = Result.Nodes[value].Text;
        assignment.Left = value;
        assignment.Right = expression();
        expect(K::Semicolon, "';' after assignment");
        result.Statements.push_back(add(std::move(assignment)));
      } else if (take(K::Semicolon)) {
        Node discarded{Kind::Discard};
        discarded.Text = Result.Nodes[value].Text;
        discarded.Left = value;
        result.Statements.push_back(add(std::move(discarded)));
      } else if (kind() == K::RightBrace) {
        result.Left = value;
        break;
      } else if (Result.Nodes[value].Form == Kind::Block ||
                 Result.Nodes[value].Form == Kind::If ||
                 Result.Nodes[value].Form == Kind::Match ||
                 Result.Nodes[value].Form == Kind::While ||
                 Result.Nodes[value].Form == Kind::Loop) {
        result.Statements.push_back(value);
      } else {
        fail("expected ';' after expression");
      }
    }
    expect(K::RightBrace, "'}'");
    --BlockDepth;
    return add(std::move(result));
  }
  auto expression(int minimum = 1) -> NodeId {
    if (++Depth > 256) {
      fail("expression nesting limit exceeded");
      --Depth;
      return NoNode;
    }
    auto left = prefix();
    while (!Failed && Precedence(kind()) >= minimum &&
           Precedence(kind()) != 0) {
      const auto operation = advance();
      const auto precedence = Precedence(operation.Kind);
      Node node{operation.Kind == K::As ? Kind::Convert : Kind::Binary};
      node.Operator = operation.Kind;
      node.Text = operation.Text;
      node.Left = left;
      if (operation.Kind == K::As) {
        node.Annotation = type();
      } else {
        node.Right = expression(precedence + 1);
      }
      left = add(std::move(node));
      if ((precedence == 3 || precedence == 4) &&
          Precedence(kind()) == precedence) {
        fail("comparison operators require explicit grouping");
      }
    }
    --Depth;
    return left;
  }
  auto prefix() -> NodeId {
    if (Failed) {
      return NoNode;
    }
    if (++PrefixDepth > 256) {
      fail("prefix nesting limit exceeded");
      --PrefixDepth;
      return NoNode;
    }
    struct PrefixGuard {
      unsigned &Depth;
      ~PrefixGuard() { --Depth; }
    } guard{PrefixDepth};
    const auto token = advance();
    Node node{Kind::Unit};
    node.Text = token.Text;
    switch (token.Kind) {
    case K::Integer:
    case K::Float:
      node.Form = token.Kind == K::Integer ? Kind::Integer : Kind::Float;
      node.Radix = token.Radix;
      node.SuffixLength = token.SuffixLength;
      return add(std::move(node));
    case K::True:
    case K::False:
      node.Form = Kind::Boolean;
      node.Boolean = token.Kind == K::True;
      return add(std::move(node));
    case K::Identifier:
      node.Form = Kind::Name;
      if (kind() == K::Colon && (kind(1) == K::Loop || kind(1) == K::While)) {
        advance();
        const auto labeled = prefix();
        if (labeled != NoNode) {
          Result.Nodes[labeled].Label = token.Text;
        }
        return labeled;
      }
      if (take(K::Path)) {
        node.Member = expect(K::Identifier, "a variant name").Text;
        node.Form = Kind::Construct;
      }
      if (!ParsingCondition && take(K::LeftBrace)) {
        node.Form = Kind::Construct;
        while (!Failed && kind() != K::RightBrace) {
          const auto field = expect(K::Identifier, "a constructor field").Text;
          node.Fields.push_back(field);
          if (take(K::Colon)) {
            node.Statements.push_back(expression());
          } else {
            Node shorthand{Kind::Name};
            shorthand.Text = field;
            node.Statements.push_back(add(std::move(shorthand)));
          }
          if (!take(K::Comma)) {
            break;
          }
        }
        expect(K::RightBrace, "'}' after constructor");
      }
      break;
    case K::LeftParen:
      if (take(K::RightParen)) {
        return add(std::move(node));
      }
      node.Form = Kind::Group;
      {
        const auto previousCondition = ParsingCondition;
        ParsingCondition = false;
        node.Left = expression();
        if (take(K::Comma)) {
          node.Form = Kind::Tuple;
          node.Statements.push_back(node.Left);
          node.Left = NoNode;
          while (!Failed && kind() != K::RightParen) {
            node.Statements.push_back(expression());
            if (!take(K::Comma)) {
              break;
            }
          }
        }
        expect(K::RightParen, "')'");
        ParsingCondition = previousCondition;
      }
      break;
    case K::LeftBracket:
      node.Form = Kind::Array;
      if (!take(K::RightBracket)) {
        const auto seed = expression();
        if (take(K::Semicolon)) {
          node.Form = Kind::Repeat;
          node.Left = seed;
          node.Right = expression();
        } else {
          node.Statements.push_back(seed);
          while (take(K::Comma) && kind() != K::RightBracket && !Failed) {
            node.Statements.push_back(expression());
          }
        }
        expect(K::RightBracket, "']' after array");
      }
      break;
    case K::Plus:
    case K::Minus:
    case K::Bang:
    case K::Tilde:
    case K::Copy:
    case K::Move:
      node.Form = Kind::Unary;
      if (token.Kind == K::Copy) {
        node.Form = Kind::Copy;
      }
      if (token.Kind == K::Move) {
        node.Form = Kind::Move;
      }
      node.Operator = token.Kind;
      node.Left = expression(12);
      return add(std::move(node));
    case K::LeftBrace: {
      const auto previousCondition = ParsingCondition;
      ParsingCondition = false;
      --Cursor;
      const auto value = block();
      ParsingCondition = previousCondition;
      return postfix(value);
    }
    case K::If:
      node.Form = Kind::If;
      {
        const auto previousCondition = ParsingCondition;
        ParsingCondition = true;
        node.Left = expression();
        ParsingCondition = previousCondition;
      }
      node.Right = block();
      if (take(K::Else)) {
        node.Alternative = kind() == K::If ? prefix() : block();
      }
      return postfix(add(std::move(node)));
    case K::While:
      node.Form = Kind::While;
      expect(K::LeftParen, "'(' before while condition");
      node.Left = expression();
      expect(K::RightParen, "')' after while condition");
      node.Right = block();
      return postfix(add(std::move(node)));
    case K::Loop:
      node.Form = Kind::Loop;
      node.Right = block();
      return postfix(add(std::move(node)));
    case K::Match:
      node.Form = Kind::Match;
      expect(K::LeftParen, "'(' before match input");
      node.Left = expression();
      expect(K::RightParen, "')' after match input");
      expect(K::LeftBrace, "'{' before match arms");
      while (!Failed && kind() != K::RightBrace) {
        Arm arm;
        arm.Pattern = pattern();
        if (take(K::If)) {
          arm.Guard = expression();
        }
        expect(K::FatArrow, "'=>' after pattern and guard");
        arm.Body = expression();
        node.Arms.push_back(arm);
        if (!take(K::Comma)) {
          break;
        }
      }
      expect(K::RightBrace, "'}' after match arms");
      return postfix(add(std::move(node)));
    default:
      fail(token.Kind == K::UnsupportedKeyword || token.Kind == K::For
               ? "this language feature is not supported in R3"
               : "expected an expression");
      return NoNode;
    }
    return postfix(add(std::move(node)));
  }
  auto postfix(NodeId value) -> NodeId {
    while (!Failed) {
      if (kind() == K::Dot) {
        Node projection{Kind::Project};
        projection.Text = advance().Text;
        projection.Left = value;
        projection.Member = advance().Text;
        if (Tokens[Cursor - 1].Kind != K::Identifier &&
            Tokens[Cursor - 1].Kind != K::Integer) {
          fail("expected a field or tuple index after '.'");
        }
        value = add(std::move(projection));
        continue;
      }
      if (kind() == K::LeftBracket) {
        Node index{Kind::Index};
        index.Text = advance().Text;
        index.Left = value;
        index.Right = expression();
        expect(K::RightBracket, "']' after index");
        value = add(std::move(index));
        continue;
      }
      if (!take(K::LeftParen)) {
        break;
      }
      Node call{Kind::Call};
      call.Text = Result.Nodes[value].Text;
      call.Left = value;
      if (kind() != K::RightParen) {
        do {
          Argument argument;
          if (kind() == K::Identifier && kind(1) == K::Colon) {
            argument.Named = true;
            argument.Label = advance().Text;
            advance();
          }
          argument.Expression = expression();
          call.Arguments.push_back(argument);
        } while (take(K::Comma) && kind() != K::RightParen && !Failed);
      }
      expect(K::RightParen, "')' after arguments");
      value = add(std::move(call));
    }
    return value;
  }
  std::span<const Lex::Token> Tokens;
  Diagnostic::Diagnostics &Diagnostics;
  Tree Result;
  std::vector<unsigned> NodeDepth;
  std::size_t Cursor = 0;
  unsigned Depth = 0;
  unsigned BlockDepth = 0;
  unsigned PrefixDepth = 0;
  unsigned PatternDepth = 0;
  unsigned TypeDepth = 0;
  bool Failed = false;
  bool ParsingCondition = false;
};
} // namespace
auto Parse(std::span<const Lex::Token> tokens,
           Diagnostic::Diagnostics &diagnostics) -> std::optional<Tree> {
  return Parser(tokens, diagnostics).run();
}
} // namespace Chtholly::Parse
