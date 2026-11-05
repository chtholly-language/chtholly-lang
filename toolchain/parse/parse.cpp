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
    if (depth > 256) {
      fail("syntax tree depth limit exceeded");
    }
    NodeDepth.push_back(depth);
    Result.Nodes.push_back(std::move(node));
    return id;
  }
  auto type() -> TypeSyntax {
    const auto first =
        Cursor < Tokens.size() ? Tokens[Cursor].Text : Source::Span{};
    if (take(K::Bang) || take(K::Identifier)) {
      return {first, true};
    }
    if (take(K::LeftParen)) {
      const auto end = expect(K::RightParen, "')' in unit type").Text;
      return {{first.Begin, end.Begin + end.Length - first.Begin}, true, true};
    }
    fail("expected a supported scalar type");
    return {};
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
        binding.Text = expect(K::Identifier, "a local name or '_'").Text;
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
      if (kind() == K::Return) {
        Node statement{Kind::Return};
        statement.Text = advance().Text;
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
                 Result.Nodes[value].Form == Kind::If) {
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
      break;
    case K::LeftParen:
      if (take(K::RightParen)) {
        return add(std::move(node));
      }
      node.Form = Kind::Group;
      node.Left = expression();
      expect(K::RightParen, "')'");
      break;
    case K::Plus:
    case K::Minus:
    case K::Bang:
    case K::Tilde:
      node.Form = Kind::Unary;
      node.Operator = token.Kind;
      node.Left = expression(12);
      return add(std::move(node));
    case K::LeftBrace:
      --Cursor;
      return block();
    case K::If:
      node.Form = Kind::If;
      node.Left = expression();
      node.Right = block();
      if (take(K::Else)) {
        node.Alternative = kind() == K::If ? prefix() : block();
      }
      return add(std::move(node));
    default:
      fail(token.Kind == K::UnsupportedKeyword
               ? "this language feature is not supported in R2"
               : "expected an expression");
      return NoNode;
    }
    auto value = add(std::move(node));
    while (!Failed && take(K::LeftParen)) {
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
  bool Failed = false;
};
} // namespace
auto Parse(std::span<const Lex::Token> tokens,
           Diagnostic::Diagnostics &diagnostics) -> std::optional<Tree> {
  return Parser(tokens, diagnostics).run();
}
} // namespace Chtholly::Parse
