// SPDX-License-Identifier: MIT OR Apache-2.0
#include "checker.h"
#include <Chtholly/Target/layout.h>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace Chtholly::Check {
using Type = Types::TypeId;
using Id = SemIR::Id;
auto Checker::resolveAlias(Id index) -> Type {
  if (AliasState[index] == 1) {
    error(Tree.Declarations[index].Name.Begin, "alias dependency cycle");
    return Type::Invalid;
  }
  const auto spelling = name(Tree.Declarations[index].Name);
  if (AliasState[index] == 2) {
    return TypeNames.at(spelling);
  }
  if (DeclarationDepth >= 256) {
    error(Tree.Declarations[index].Name.Begin,
          "declaration dependency resource budget exhausted");
    return Type::Invalid;
  }
  ++DeclarationDepth;
  AliasState[index] = 1;
  const auto result = type(Tree.Declarations[index].Type, Type::Invalid);
  AliasState[index] = 2;
  TypeNames[spelling] = result;
  --DeclarationDepth;
  return result;
}
auto Checker::constant(Id index) -> std::optional<Numeric::Value> {
  if (ConstantState[index] == 1) {
    error(Tree.Declarations[index].Name.Begin, "const dependency cycle");
    return std::nullopt;
  }
  if (ConstantState[index] == 2) {
    return ConstantValues[index];
  }
  if (DeclarationDepth >= 256) {
    error(Tree.Declarations[index].Name.Begin,
          "declaration dependency resource budget exhausted");
    return std::nullopt;
  }
  ++DeclarationDepth;
  ConstantState[index] = 1;
  const auto &syntax = Tree.Declarations[index];
  const auto valueType = type(syntax.Type, Type::Invalid);
  if (valueType.owner() != nullptr || valueType == Type::Never ||
      valueType == Type::Invalid) {
    error(syntax.Name.Begin,
          "R3 const requires an explicitly typed constructible primitive");
  }
  const auto savedScopes = std::move(PureScopes);
  const auto savedBudget = Budget;
  PureScopes.clear();
  Budget = Numeric::Budget(Budget.limits());
  auto value = pure(syntax.Initializer, valueType, true);
  PureScopes = std::move(savedScopes);
  Budget = savedBudget;
  if (PureNodes[syntax.Initializer].ValueType != valueType) {
    error(syntax.Name.Begin,
          "const initializer type does not match its annotation");
  }
  ConstantState[index] = 2;
  ConstantValues[index] = value;
  --DeclarationDepth;
  return value;
}
auto Checker::length(Id node) -> std::optional<std::uint64_t> {
  const auto savedScopes = std::move(PureScopes);
  const auto savedBudget = Budget;
  PureScopes.clear();
  Budget = Numeric::Budget(Budget.limits());
  const auto value = pure(node, Type::USize, true);
  PureScopes = std::move(savedScopes);
  Budget = savedBudget;
  if (!value || value->type() != Target::Scalar::USize) {
    fail(node, "array length requires a target usize constant");
    return std::nullopt;
  }
  return value->bits()[0];
}
auto Checker::declarations() -> void {
  const auto count = Tree.Declarations.size();
  AliasState.resize(count, 0);
  ConstantState.resize(count, 0);
  ConstantValues.resize(count);
  std::unordered_set<std::string> names;
  for (Id i = 0; i < count; ++i) {
    const auto &declaration = Tree.Declarations[i];
    if (declaration.Form == Parse::Declaration::Kind::CopyImpl) {
      continue;
    }
    const auto spelling = name(declaration.Name);
    if (spelling == "_" || !names.insert(spelling).second ||
        Types::Lookup(spelling)) {
      error(declaration.Name.Begin, "duplicate or reserved declaration name");
    }
    switch (declaration.Form) {
    case Parse::Declaration::Kind::Struct:
    case Parse::Declaration::Kind::Enum:
      TypeNames[spelling] = TypeBuilder.declare(
          declaration.Form == Parse::Declaration::Kind::Struct
              ? Types::Kind::Struct
              : Types::Kind::Enum,
          spelling);
      break;
    case Parse::Declaration::Kind::Alias:
      Aliases[spelling] = i;
      break;
    case Parse::Declaration::Kind::Const:
      Constants[spelling] = i;
      break;
    default:
      break;
    }
  }
  if (Failed) {
    return;
  }
  for (Id index = 0; index < count; ++index) {
    if (Tree.Declarations[index].Form == Parse::Declaration::Kind::Alias) {
      resolveAlias(index);
    }
  }
  for (Id index = 0; index < count; ++index) {
    if (Tree.Declarations[index].Form == Parse::Declaration::Kind::Const) {
      constant(index);
    }
  }
  const auto completeFields = [&](const std::vector<Parse::Field> &fields) {
    std::vector<Types::Field> result;
    std::unordered_set<std::string> fieldNames;
    for (const auto &field : fields) {
      const auto spelling = name(field.Name);
      if (spelling == "_" || !fieldNames.insert(spelling).second) {
        error(field.Name.Begin, "duplicate or invalid field name");
      }
      result.push_back({spelling, type(field.Type, Type::Invalid)});
    }
    return result;
  };
  for (const auto &declaration : Tree.Declarations) {
    if (declaration.Form != Parse::Declaration::Kind::Struct &&
        declaration.Form != Parse::Declaration::Kind::Enum) {
      continue;
    }
    std::vector<Types::Variant> variants;
    std::unordered_set<std::string> variantNames;
    for (const auto &variant : declaration.Variants) {
      const auto spelling = name(variant.Name);
      if (!variantNames.insert(spelling).second) {
        error(variant.Name.Begin, "duplicate variant name");
      }
      variants.push_back({spelling, completeFields(variant.Fields), false});
    }
    TypeBuilder.define(TypeNames.at(name(declaration.Name)),
                       completeFields(declaration.Fields), std::move(variants));
  }
  for (const auto &declaration : Tree.Declarations) {
    if (declaration.Form != Parse::Declaration::Kind::CopyImpl) {
      continue;
    }
    // The path resolves to the reserved prelude identity; a local core name
    // shadows it rather than granting privileges by matching the spelling.
    const bool coreFunction =
        std::any_of(Tree.Functions.begin(), Tree.Functions.end(),
                    [&](const Parse::Function &function) {
                      return name(function.Name) == "core";
                    });
    if (name(declaration.Name) != "core" ||
        name(declaration.Member) != "Copy" || names.contains("core") ||
        coreFunction) {
      error(declaration.Name.Begin,
            "implementation must select the canonical core::Copy identity");
    } else if (!TypeBuilder.requestCopy(
                   type(declaration.Type, Type::Invalid))) {
      error(declaration.Type.Text.Begin,
            "Copy implementation requires a local nominal type and unique "
            "evidence");
    }
  }
  std::vector<Types::Error> errors;
  TypeBuilder.complete(errors);
  for (const auto &failure : errors) {
    error(0, failure.Message);
  }
}

auto Checker::pure(Id id, Type context,
                   bool executable) -> std::optional<Numeric::Value> {
  if (++ExpressionDepth > 256) {
    --ExpressionDepth;
    fail(id, "constant expression nesting resource budget exhausted");
    return std::nullopt;
  }
  struct DepthGuard {
    unsigned &Depth;
    ~DepthGuard() { --Depth; }
  } depthGuard{ExpressionDepth};
  const auto &syntax = Tree.Nodes.at(id);
  auto &record = PureNodes[id];
  record = SemIR::Node{};
  std::optional<Numeric::Value> value;
  const auto finishPure = [&](Numeric::Result result) -> void {
    if (result.Error == Numeric::Failure::ResourceExceeded ||
        result.Error == Numeric::Failure::InternalError ||
        (executable && result.Error != Numeric::Failure::None &&
         RecoveryRoot != id)) {
      fail(id, std::string(Numeric::FailureName(result.Error)));
    }
    record.ConstantFailure = result.Error;
    value = std::move(result.ValueResult);
  };
  if (!Budget.consume()) {
    fail(id, "constant evaluation resource budget exhausted");
    return std::nullopt;
  }
  record.ValueType = Type::Invalid;
  switch (syntax.Form) {
  case Parse::Kind::Integer:
  case Parse::Kind::Float: {
    const auto wasMath = MathContext;
    literal(id, context, false, &record);
    MathContext = wasMath;
    value = record.Constant;
    break;
  }
  case Parse::Kind::Boolean:
    record.ValueType = Type::Bool;
    value = Numeric::Value::Boolean(syntax.Boolean);
    break;
  case Parse::Kind::Unit:
    record.ValueType = Type::Unit;
    value = Numeric::Value::Unit();
    break;
  case Parse::Kind::Name: {
    const auto spelling = name(syntax.Text);
    bool foundLocal = false;
    for (auto scope = PureScopes.rbegin(); scope != PureScopes.rend();
         ++scope) {
      if (const auto found = scope->find(spelling); found != scope->end()) {
        record.ValueType = found->second.ValueType;
        value = found->second.Value;
        foundLocal = true;
        break;
      }
    }
    if (!foundLocal) {
      if (const auto found = Constants.find(spelling);
          found != Constants.end()) {
        // The recursive query owns its result in the stable constant cache.
        // Read that cache after completion, so the engagement check and the
        // payload copy refer to the same object across the query boundary.
        constant(found->second);
        const auto &dependency = ConstantValues[found->second];
        if (dependency.has_value()) {
          value.emplace(*dependency);
          record.ValueType = Target::SourceType(dependency->type());
        } else {
          record.ValueType =
              type(Tree.Declarations[found->second].Type, Type::Invalid);
        }
      } else {
        fail(id, "name is not in the pure constant domain");
      }
    }
    break;
  }
  case Parse::Kind::Group:
    value = pure(syntax.Left, context, executable);
    record.ValueType = PureNodes[syntax.Left].ValueType;
    break;
  case Parse::Kind::Unary: {
    const auto operand = root(syntax.Left);
    const auto known = hint(operand);
    const auto signedContext = known == Type::Invalid ? context : known;
    const bool candidate =
        Tree.Nodes[operand].Form == Parse::Kind::Integer &&
        syntax.Operator == Lex::TokenKind::Minus &&
        (Types::IsSigned(signedContext) || signedContext == Type::Invalid);
    if (candidate) {
      literal(operand, context, true, &PureNodes[operand]);
      value = PureNodes[operand].Constant;
      record.ValueType = PureNodes[operand].ValueType;
    } else {
      const auto operandValue =
          pure(syntax.Left,
               syntax.Operator == Lex::TokenKind::Bang ? Type::Bool : context,
               executable);
      record.ValueType = PureNodes[syntax.Left].ValueType;
      const auto op = operation(syntax.Operator, true);
      const bool eligible = op == Numeric::Operation::Not
                                ? record.ValueType == Type::Bool
                            : op == Numeric::Operation::Complement
                                ? Types::IsInteger(record.ValueType)
                                : Types::IsNumeric(record.ValueType) ||
                                      (MathContext && operandValue &&
                                       operandValue->isMathematical());
      if (!eligible) {
        fail(id, "invalid pure unary operation");
      }
      if (operandValue && eligible) {
        finishPure(Numeric::Unary(op, *operandValue, Budget));
      }
    }
    break;
  }
  case Parse::Kind::Binary: {
    const auto op = operation(syntax.Operator);
    const bool logical = op == Numeric::Operation::LogicalAnd ||
                         op == Numeric::Operation::LogicalOr;
    const bool compare =
        op >= Numeric::Operation::Less && op <= Numeric::Operation::NotEqual;
    const bool shift = op == Numeric::Operation::ShiftLeft ||
                       op == Numeric::Operation::ShiftRight;
    auto expected = logical ? Type::Bool : compare ? Type::Invalid : context;
    std::unordered_map<std::string, Type> known;
    for (const auto &scope : PureScopes) {
      for (const auto &[spelling, local] : scope) {
        known[spelling] = local.ValueType;
      }
    }
    if (!logical) {
      auto candidate = hint(syntax.Left, known, false);
      if (candidate == Type::Invalid && !shift) {
        candidate = hint(syntax.Right, known, false);
      }
      if (candidate != Type::Invalid && candidate != Type::Never) {
        expected = candidate;
      }
    }
    // Typed const names and suffixes supply context, but ordinary runtime
    // locals never participate in this evaluator.
    const auto left = pure(syntax.Left, expected, executable);
    const auto leftType = PureNodes[syntax.Left].ValueType;
    bool rightExec = executable;
    if (logical && left && leftType == Type::Bool) {
      rightExec &= op == Numeric::Operation::LogicalAnd ? left->boolean()
                                                        : !left->boolean();
    }
    const auto right =
        pure(syntax.Right, shift ? Type::Invalid : leftType, rightExec);
    const auto rightType = PureNodes[syntax.Right].ValueType;
    record.ValueType = logical || compare ? Type::Bool : leftType;
    const bool equality =
        op == Numeric::Operation::Equal || op == Numeric::Operation::NotEqual;
    bool eligible = false;
    if (shift) {
      eligible = Types::IsInteger(leftType) && Types::IsInteger(rightType);
    } else if (leftType == rightType) {
      if (logical) {
        eligible = leftType == Type::Bool;
      } else if (compare) {
        eligible =
            Types::IsNumeric(leftType) || (equality && leftType == Type::Bool);
      } else if (op == Numeric::Operation::BitAnd ||
                 op == Numeric::Operation::BitOr ||
                 op == Numeric::Operation::BitXor ||
                 op == Numeric::Operation::Remainder) {
        eligible = Types::IsInteger(leftType);
      } else {
        eligible = Types::IsNumeric(leftType);
      }
    }
    const bool math =
        MathContext && leftType == Type::Invalid && rightType == Type::Invalid;
    eligible |= math && (op == Numeric::Operation::Add ||
                         op == Numeric::Operation::Subtract ||
                         op == Numeric::Operation::Multiply ||
                         op == Numeric::Operation::Divide ||
                         op == Numeric::Operation::Remainder);
    if (!eligible) {
      fail(id, "invalid pure binary operand types");
    }
    if (logical && left && !rightExec && executable) {
      value = left;
    } else if (eligible && left && right) {
      finishPure(Numeric::Binary(op, *left, *right, Budget));
    }
    break;
  }
  case Parse::Kind::Convert: {
    record.ValueType = type(syntax.Annotation, Type::Invalid);
    if (mathematical(syntax.Left)) {
      const auto oldMath = MathContext;
      MathContext = true;
      const auto source = pure(syntax.Left, Type::Invalid, executable);
      MathContext = oldMath;
      if (!Types::IsNumeric(record.ValueType)) {
        fail(id, "pure as requires a numeric target");
      } else if (source) {
        finishPure(Numeric::Convert(*source, record.ValueType, Budget));
      }
    } else {
      const auto source = pure(syntax.Left, Type::Invalid, executable);
      if (!Types::IsNumeric(record.ValueType) ||
          !Types::IsNumeric(PureNodes[syntax.Left].ValueType)) {
        fail(id, "pure as requires numeric types");
      } else if (source) {
        finishPure(Numeric::Convert(*source, record.ValueType, Budget));
      }
    }
    break;
  }
  case Parse::Kind::If: {
    const auto condition = pure(syntax.Left, Type::Bool, executable);
    if (PureNodes[syntax.Left].ValueType != Type::Bool) {
      fail(id, "pure if condition must be bool");
    }
    auto branchContext =
        syntax.Alternative == SemIR::None ? Type::Unit : context;
    if (branchContext == Type::Invalid) {
      std::unordered_map<std::string, Type> known;
      for (const auto &scope : PureScopes) {
        for (const auto &[spelling, local] : scope) {
          known[spelling] = local.ValueType;
        }
      }
      branchContext = hint(syntax.Right, known, false);
      if (branchContext == Type::Invalid && syntax.Alternative != SemIR::None) {
        branchContext = hint(syntax.Alternative, known, false);
      }
    }
    const auto first = pure(syntax.Right, branchContext,
                            executable && condition && condition->boolean());
    const auto second =
        syntax.Alternative == SemIR::None
            ? std::optional<Numeric::Value>(Numeric::Value::Unit())
            : pure(syntax.Alternative, branchContext,
                   executable && condition && !condition->boolean());
    record.ValueType = PureNodes[syntax.Right].ValueType;
    const auto otherType = syntax.Alternative == SemIR::None
                               ? Type::Unit
                               : PureNodes[syntax.Alternative].ValueType;
    if (record.ValueType != otherType) {
      fail(id, "pure if branches must have the same type");
    }
    if (condition) {
      value = condition->boolean() ? first : second;
    }
    break;
  }
  case Parse::Kind::Block: {
    PureScopes.emplace_back();
    for (const auto statement : syntax.Statements) {
      const auto &item = Tree.Nodes[statement];
      if (item.Form == Parse::Kind::Bind && !item.Mutable &&
          item.Pattern != Parse::NoPattern &&
          Tree.Patterns[item.Pattern].Form == Parse::PatternKind::Binding &&
          !Tree.Patterns[item.Pattern].Mutable) {
        const auto expected = type(item.Annotation, Type::Invalid);
        const auto saved = RecoveryRoot;
        if (item.Alternative != SemIR::None) {
          RecoveryRoot = root(item.Left);
        }
        auto initial = pure(item.Left, expected, executable);
        RecoveryRoot = saved;
        const auto initialType = PureNodes[item.Left].ValueType;
        if (initialType.owner() != nullptr || initialType == Type::Never ||
            initialType == Type::Invalid) {
          fail(statement, "pure let requires a constructible primitive");
        }
        if (item.Alternative != SemIR::None) {
          if (Tree.Nodes[root(item.Left)].Form != Parse::Kind::Convert) {
            fail(statement, "only as can recover normally in a pure block");
          }
          const auto alternative =
              pure(item.Alternative, initialType, executable && !initial);
          if (!initial) {
            initial = alternative;
          }
        }
        const auto spelling = name(Tree.Patterns[item.Pattern].Text);
        if (spelling != "_" &&
            !PureScopes.back()
                 .emplace(spelling, PureLocal{initialType, initial})
                 .second) {
          fail(statement, "duplicate pure local binding");
        }
      } else if (item.Form == Parse::Kind::Discard) {
        pure(item.Left, Type::Invalid, executable);
      } else {
        fail(statement, "constant blocks support only immutable scalar let and "
                        "pure expression statements");
      }
    }
    value = syntax.Left == SemIR::None
                ? std::optional<Numeric::Value>(Numeric::Value::Unit())
                : pure(syntax.Left, context, executable);
    record.ValueType = syntax.Left == SemIR::None
                           ? Type::Unit
                           : PureNodes[syntax.Left].ValueType;
    PureScopes.pop_back();
    break;
  }
  default:
    fail(id, "expression is outside the R3 pure constant subset");
    break;
  }
  if (context != Type::Invalid && record.ValueType != Type::Invalid &&
      context != record.ValueType) {
    fail(id, "pure expression type does not match its context");
  }
  record.Constant = value;
  return value;
}
} // namespace Chtholly::Check
