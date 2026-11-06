// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/IR/ir.h>

#include <algorithm>
#include <iterator>
#include <set>
#include <stdexcept>
#include <tuple>

namespace Chtholly::IR {
namespace {
using O = Numeric::Operation;
auto Require(bool condition, const char *reason) -> void {
  if (!condition) {
    throw std::logic_error(reason);
  }
}
auto OperandTypes(O op, Type left, Type right, Type result,
                  bool checked) -> bool {
  const bool compare = op >= O::Less && op <= O::NotEqual;
  if (compare) {
    return !checked && left == right && result == Type::Bool &&
           (Target::IsNumeric(left) ||
            ((op == O::Equal || op == O::NotEqual) && left == Type::Bool));
  }
  if (op == O::ShiftLeft || op == O::ShiftRight) {
    return checked && Target::IsInteger(left) && Target::IsInteger(right) &&
           result == left;
  }
  if (left != right || result != left) {
    return false;
  }
  if (op == O::BitAnd || op == O::BitOr || op == O::BitXor) {
    return !checked && Target::IsInteger(left);
  }
  if (op == O::Remainder) {
    return checked && Target::IsInteger(left);
  }
  if (op == O::Add || op == O::Subtract || op == O::Multiply ||
      op == O::Divide) {
    return Target::IsInteger(left) ? checked
                                   : !checked && Target::IsFloat(left);
  }
  return false;
}
auto UnaryTypes(O op, Type source, Type result, bool checked) -> bool {
  if (source != result) {
    return false;
  }
  if (op == O::Not) {
    return !checked && source == Type::Bool;
  }
  if (op == O::Complement) {
    return !checked && Target::IsInteger(source);
  }
  if (op == O::Plus) {
    return !checked && Target::IsNumeric(source);
  }
  if (op == O::Negate) {
    return Target::IsInteger(source) ? checked
                                     : !checked && Target::IsFloat(source);
  }
  return false;
}

// Keep one immediate dominator per block, rather than a set containing every
// dominator of every block. Pattern expansion must not create quadratic space
// before Flow gets the opportunity to enforce its own resource limits.
class Dominance {
public:
  Dominance(const Function &function,
            const std::vector<std::vector<Id>> &predecessors)
      : Reachable(function.Blocks.size(), false), Enter(function.Blocks.size()),
        Leave(function.Blocks.size()) {
    struct Visit {
      Id Block;
      unsigned Next = 0;
    };
    std::vector<Visit> pending{{0}};
    std::vector<Id> postorder;
    Reachable[0] = true;
    while (!pending.empty()) {
      auto &visit = pending.back();
      const auto &end = function.Blocks[visit.Block].End;
      unsigned count = 0;
      if (end.Kind == Exit::Jump) {
        count = 1;
      } else if (end.Kind == Exit::Branch || end.Kind == Exit::Checked ||
                 end.Kind == Exit::Bounds || end.Kind == Exit::Variant) {
        count = 2;
      }
      if (visit.Next == count) {
        postorder.push_back(visit.Block);
        pending.pop_back();
        continue;
      }
      const auto next =
          visit.Next++ == 0 ? end.First.Target : end.Second.Target;
      if (!Reachable[next]) {
        Reachable[next] = true;
        pending.push_back({next});
      }
    }
    const std::vector<Id> order(postorder.rbegin(), postorder.rend());
    std::vector<std::size_t> rank(function.Blocks.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
      rank[order[i]] = i;
    }
    std::vector<Id> immediate(function.Blocks.size(), None);
    immediate[0] = 0;
    const auto intersect = [&](Id first, Id second) {
      while (first != second) {
        while (rank[first] > rank[second]) {
          first = immediate[first];
        }
        while (rank[second] > rank[first]) {
          second = immediate[second];
        }
      }
      return first;
    };
    bool changed = true;
    while (changed) {
      changed = false;
      for (std::size_t i = 1; i < order.size(); ++i) {
        const auto block = order[i];
        Id parent = None;
        for (const auto predecessor : predecessors[block]) {
          if (immediate[predecessor] != None) {
            parent =
                parent == None ? predecessor : intersect(parent, predecessor);
          }
        }
        if (parent != None && parent != immediate[block]) {
          immediate[block] = parent;
          changed = true;
        }
      }
    }
    std::vector<std::vector<Id>> children(function.Blocks.size());
    for (std::size_t i = 1; i < order.size(); ++i) {
      const auto block = order[i];
      Require(immediate[block] != None, "reachable block has no dominator");
      children[immediate[block]].push_back(block);
    }
    // Tree intervals make each later dominance query constant-time.
    std::vector<std::pair<Id, bool>> tree{{0, false}};
    std::size_t tick = 0;
    while (!tree.empty()) {
      const auto [block, finished] = tree.back();
      tree.pop_back();
      if (finished) {
        Leave[block] = tick++;
        continue;
      }
      Enter[block] = tick++;
      tree.push_back({block, true});
      for (const auto child : children[block]) {
        tree.push_back({child, false});
      }
    }
  }
  auto reachable(Id block) const -> bool { return Reachable[block]; }
  auto contains(Id owner, Id block) const -> bool {
    return Reachable[owner] && Enter[owner] <= Enter[block] &&
           Leave[block] <= Leave[owner];
  }

private:
  std::vector<bool> Reachable;
  std::vector<std::size_t> Enter;
  std::vector<std::size_t> Leave;
};

auto VerifyFunction(const Draft &program, const Function &function) -> void {
  const auto blocks = function.Blocks.size();
  Require(program.Types.has_value(), "IR has no completed type table");
  Require(function.ValuePlaces.size() == function.Values.size(),
          "missing value/place ownership map");
  Require(function.LocalRoots.size() == function.Locals.size() &&
              function.LocalWritable.size() == function.Locals.size(),
          "missing local place declarations");
  std::set<std::tuple<Id, Projection, std::uint64_t, Id, Id>> projections;
  for (Id i = 0; i < function.Places.size(); ++i) {
    const auto &place = function.Places[i];
    Require(program.Types->contains(place.ValueType),
            "place has a foreign or invalid type");
    if (place.Form == Projection::Root) {
      Require(place.Parent == None, "root place has a parent");
      if (place.Local != None) {
        Require(place.Local < function.Locals.size() &&
                    function.LocalRoots[place.Local] == i &&
                    function.Locals[place.Local] == place.ValueType &&
                    place.ValueOwner == None &&
                    place.Writable == function.LocalWritable[place.Local],
                "invalid local storage root or permissions");
      } else {
        Require(place.ValueOwner < function.Values.size() &&
                    function.ValuePlaces[place.ValueOwner] == i &&
                    function.Values[place.ValueOwner] == place.ValueType,
                "invalid owning value root");
      }
    } else {
      Require(place.Parent < i, "projection is cyclic or has a foreign parent");
      Require(place.Local == None && place.ValueOwner == None,
              "projection claims an independent storage owner");
      Require(place.Form == Projection::VariantField || place.Variant == None,
              "non-variant projection carries a variant identity");
      Require(place.Form == Projection::DynamicElement ||
                  place.IndexValue == None,
              "fixed projection carries a dynamic index");
      const auto index =
          place.Form == Projection::DynamicElement ? 0 : place.Index;
      const auto variant =
          place.Form == Projection::VariantField ? place.Variant : None;
      const auto dynamicIndex =
          place.Form == Projection::DynamicElement ? place.IndexValue : None;
      Require(
          projections
              .emplace(place.Parent, place.Form, index, variant, dynamicIndex)
              .second,
          "duplicate logical projection identity");
      const auto parentType = function.Places[place.Parent].ValueType;
      Require(parentType.owner() != nullptr,
              "projection parent is not aggregate");
      const auto &parent = program.Types->get(parentType);
      if (place.Form == Projection::Element ||
          place.Form == Projection::DynamicElement) {
        Require(parent.Form == Types::Kind::Array &&
                    parent.Element == place.ValueType,
                "array projection type mismatch");
        if (place.Form == Projection::Element) {
          Require(place.Index < parent.Length,
                  "fixed element is out of bounds");
        } else {
          Require(place.IndexValue < function.Values.size() &&
                      function.Values[place.IndexValue] == Type::USize,
                  "dynamic index is not normalized usize");
        }
      } else if (place.Form == Projection::Field) {
        Require((parent.Form == Types::Kind::Struct ||
                 parent.Form == Types::Kind::Tuple) &&
                    place.Index < parent.Fields.size() &&
                    parent.Fields[place.Index].Type == place.ValueType,
                "field projection mismatch");
      } else if (place.Form == Projection::VariantField) {
        Require(parent.Form == Types::Kind::Enum &&
                    place.Variant < parent.Variants.size() &&
                    place.Index <
                        parent.Variants[place.Variant].Fields.size() &&
                    parent.Variants[place.Variant].Fields[place.Index].Type ==
                        place.ValueType,
                "variant projection mismatch");
      } else {
        Require(false, "unknown place projection");
      }
      Require(!place.Writable || function.Places[place.Parent].Writable,
              "projection grants extra write permission");
    }
  }
  for (Id value = 0; value < function.Values.size(); ++value) {
    Require(program.Types->contains(function.Values[value]),
            "value has a foreign or invalid type");
    const auto place = function.ValuePlaces[value];
    Require(function.Values[value].owner() == nullptr
                ? place == None
                : place < function.Places.size() &&
                      function.Places[place].ValueOwner == value,
            "owning value has no unique storage identity");
  }
  for (Id local = 0; local < function.Locals.size(); ++local) {
    const auto root = function.LocalRoots[local];
    Require(root < function.Places.size() &&
                function.Places[root].Form == Projection::Root &&
                function.Places[root].Local == local,
            "local has no unique owning root");
  }
  bool inhabited = true;
  for (const auto &group : function.BindingGroups) {
    Require(!group.CreationOrders.empty(), "empty binding group");
    std::set<Id> expected;
    for (std::size_t alternative = 0; alternative < group.CreationOrders.size();
         ++alternative) {
      std::set<Id> actual;
      for (const auto place : group.CreationOrders[alternative]) {
        Require(place < function.Places.size() &&
                    function.Places[place].Local != None &&
                    actual.insert(place).second,
                "invalid or repeated binding responsibility");
      }
      if (alternative == 0) {
        expected = actual;
      } else {
        Require(actual == expected,
                "binding group alternatives disagree about owned locals");
      }
    }
  }
  for (const auto local : function.Locals) {
    Require(program.Types->contains(local), "invalid local type");
  }
  std::set<Id> parameterLocals;
  for (const auto &parameter : function.Parameters) {
    Require(parameter.Local < function.Locals.size(),
            "parameter local ID out of range");
    Require(function.Locals[parameter.Local] == parameter.ValueType,
            "parameter/local type mismatch");
    Require(program.Types->contains(parameter.ValueType),
            "invalid parameter type");
    Require(parameterLocals.insert(parameter.Local).second,
            "parameters share a local slot");
    inhabited &= program.Types->inhabited(parameter.ValueType);
  }
  Require(inhabited == function.HasNormalEntry,
          "normal entry proof disagrees with parameter types");
  Require(program.Types->contains(function.ReturnType), "invalid return type");
  if (!inhabited) {
    Require(blocks == 0 && function.Values.empty(),
            "uninhabited function has executable values or blocks");
    return;
  }
  Require(blocks != 0, "function has no entry block");
  Require(function.Blocks[0].Parameters.size() == function.Parameters.size(),
          "entry parameter count mismatch");
  const auto type = [&](Id value) -> Type {
    Require(value < function.Values.size(),
            "value ID out of range or foreign owner");
    const auto result = function.Values[value];
    Require(program.Types->contains(result) && program.Types->inhabited(result),
            "value has no constructible source type");
    return result;
  };
  for (std::size_t i = 0; i < function.Parameters.size(); ++i) {
    Require(type(function.Blocks[0].Parameters[i]) ==
                function.Parameters[i].ValueType,
            "entry parameter type mismatch");
  }
  std::vector<std::vector<Id>> predecessors(blocks);
  std::vector<std::pair<Id, std::size_t>> definitions(function.Values.size(),
                                                      {None, 0});
  const auto define = [&](Id value, Id block, std::size_t order) {
    type(value);
    Require(definitions[value].first == None, "value has multiple definitions");
    definitions[value] = {block, order};
  };
  for (Id b = 0; b < blocks; ++b) {
    const auto &block = function.Blocks[b];
    for (const auto value : block.Parameters) {
      define(value, b, 0);
    }
    const auto edge = [&](const Edge &destination,
                          std::optional<Type> implicit = std::nullopt) {
      Require(destination.Target < blocks && destination.Target != 0,
              "invalid branch target");
      predecessors[destination.Target].push_back(b);
      const auto &parameters = function.Blocks[destination.Target].Parameters;
      if (implicit) {
        Require(destination.Arguments.empty() && parameters.size() == 1 &&
                    type(parameters[0]) == *implicit,
                "checked edge result parameter mismatch");
      } else {
        Require(parameters.size() == destination.Arguments.size(),
                "edge argument count mismatch");
        for (std::size_t i = 0; i < parameters.size(); ++i) {
          Require(type(parameters[i]) == type(destination.Arguments[i]),
                  "edge argument type mismatch");
        }
      }
    };
    for (std::size_t i = 0; i < block.Instructions.size(); ++i) {
      const auto &instruction = block.Instructions[i];
      if (instruction.Result != None) {
        define(instruction.Result, b, i + 1);
      }
      const auto count = instruction.Operands.size();
      for (const auto value : instruction.Operands) {
        type(value);
      }
      switch (instruction.Code) {
      case Opcode::EndBindings:
        Require(count == 1 && type(instruction.Operands[0]) == Type::U32 &&
                    instruction.Entity < function.BindingGroups.size() &&
                    instruction.Result == None,
                "invalid selected binding cleanup");
        break;
      case Opcode::Constant:
        Require(count == 0 && instruction.Constant &&
                    instruction.Result != None &&
                    Target::SourceType(instruction.Constant->type()) ==
                        type(instruction.Result),
                "invalid constant");
        break;
      case Opcode::Load:
      case Opcode::Move:
      case Opcode::Observe:
      case Opcode::Store:
        Require(instruction.Entity < function.Places.size(),
                "place ID out of range");
        if (instruction.Code != Opcode::Store) {
          Require(count == 0 &&
                      type(instruction.Result) ==
                          function.Places[instruction.Entity].ValueType,
                  "load type mismatch");
          if (instruction.Code != Opcode::Move) {
            Require(program.Types->copy(type(instruction.Result)),
                    "copy or observation of a non-Copy value");
          }
        } else {
          Require(count == 1 && instruction.Result == None &&
                      type(instruction.Operands[0]) ==
                          function.Places[instruction.Entity].ValueType,
                  "store type mismatch");
        }
        break;
      case Opcode::Live:
      case Opcode::Dead:
      case Opcode::Protect:
      case Opcode::Unprotect:
      case Opcode::Locate:
      case Opcode::EndValue:
        Require(count == 0 && instruction.Result == None &&
                    instruction.Entity < function.Places.size(),
                "invalid storage/lifetime event");
        if (instruction.Code == Opcode::Live ||
            instruction.Code == Opcode::Dead ||
            instruction.Code == Opcode::EndValue) {
          Require(function.Places[instruction.Entity].Form == Projection::Root,
                  "lifetime event requires storage root");
        }
        break;
      case Opcode::Construct:
      case Opcode::Repeat: {
        const auto resultType = type(instruction.Result);
        Require(resultType.owner() != nullptr,
                "aggregate construction has scalar result");
        const auto &aggregate = program.Types->get(resultType);
        if (instruction.Code == Opcode::Repeat) {
          Require(aggregate.Form == Types::Kind::Array && count == 1 &&
                      instruction.Count == aggregate.Length &&
                      type(instruction.Operands[0]) == aggregate.Element &&
                      (aggregate.Length <= 1 ||
                       program.Types->copy(aggregate.Element)),
                  "invalid repeated array construction");
        } else {
          std::vector<Type> fields;
          if (aggregate.Form == Types::Kind::Array) {
            Require(count == aggregate.Length, "array list count mismatch");
            for (const auto value : instruction.Operands) {
              Require(type(value) == aggregate.Element,
                      "array element type mismatch");
            }
          } else {
            if (aggregate.Form == Types::Kind::Enum) {
              Require(instruction.Entity < aggregate.Variants.size() &&
                          aggregate.Variants[instruction.Entity].Inhabited,
                      "constructor selects an uninhabited or unknown variant");
              for (const auto &field :
                   aggregate.Variants[instruction.Entity].Fields) {
                fields.push_back(field.Type);
              }
            } else {
              for (const auto &field : aggregate.Fields) {
                fields.push_back(field.Type);
              }
            }
            Require(count == fields.size() && instruction.Slots.size() == count,
                    "constructor field count mismatch");
            std::set<Id> slots;
            for (std::size_t f = 0; f < count; ++f) {
              Require(instruction.Slots[f] < fields.size() &&
                          slots.insert(instruction.Slots[f]).second &&
                          type(instruction.Operands[f]) ==
                              fields[instruction.Slots[f]],
                      "invalid constructor field mapping");
            }
          }
        }
        break;
      }
      case Opcode::Unary:
        Require(count == 1 && UnaryTypes(instruction.Operation,
                                         type(instruction.Operands[0]),
                                         type(instruction.Result), false),
                "invalid unary instruction");
        break;
      case Opcode::Binary:
        Require(count == 2 && OperandTypes(instruction.Operation,
                                           type(instruction.Operands[0]),
                                           type(instruction.Operands[1]),
                                           type(instruction.Result), false),
                "invalid binary instruction");
        break;
      case Opcode::Call: {
        Require(instruction.Entity < program.Functions.size(),
                "callee ID out of range");
        const auto &callee = program.Functions[instruction.Entity];
        Require(callee.HasNormalEntry && count == callee.Parameters.size(),
                "call cannot form all parameters");
        for (std::size_t p = 0; p < count; ++p) {
          Require(type(instruction.Operands[p]) ==
                      callee.Parameters[p].ValueType,
                  "call argument type mismatch");
        }
        if (!program.Types->inhabited(callee.ReturnType)) {
          Require(instruction.Result == None &&
                      i + 1 == block.Instructions.size() &&
                      block.End.Kind == Exit::Unreachable,
                  "noreturn call has a normal successor");
        } else {
          Require(type(instruction.Result) == callee.ReturnType,
                  "call result type mismatch");
        }
        break;
      }
      default:
        Require(false, "unknown instruction opcode");
        break;
      }
    }
    const auto &end = block.End;
    for (const auto value : end.Operands) {
      type(value);
    }
    switch (end.Kind) {
    case Exit::Bounds: {
      Require(end.Operands.size() == 1 &&
                  Types::IsInteger(type(end.Operands[0])) &&
                  end.Place < function.Places.size(),
              "invalid bounds operation");
      const auto sourceType = function.Places[end.Place].ValueType;
      Require(sourceType.owner() != nullptr,
              "bounds source is not an aggregate");
      const auto &source = program.Types->get(sourceType);
      Require(source.Form == Types::Kind::Array && source.Length == end.Count,
              "bounds length disagrees with source type");
      Require(end.Location.Line != 0 && end.Location.Column != 0,
              "bounds check has no source location");
      edge(end.First, Type::USize);
      edge(end.Second, Type::U32);
      break;
    }
    case Exit::Variant:
      Require(end.Operands.empty() && end.Place < function.Places.size() &&
                  function.Places[end.Place].ValueType.owner() != nullptr,
              "invalid enum test");
      Require(program.Types->get(function.Places[end.Place].ValueType).Form ==
                      Types::Kind::Enum &&
                  end.Variant <
                      program.Types->get(function.Places[end.Place].ValueType)
                          .Variants.size(),
              "invalid variant test identity");
      edge(end.First);
      edge(end.Second);
      break;
    case Exit::Missing:
      Require(false, "block has no terminator");
      break;
    case Exit::Jump:
      Require(end.Operands.empty(), "jump has extra operands");
      edge(end.First);
      break;
    case Exit::Branch:
      Require(end.Operands.size() == 1 && type(end.Operands[0]) == Type::Bool,
              "branch condition is not bool");
      edge(end.First);
      edge(end.Second);
      break;
    case Exit::Checked:
      Require(end.Operands.size() == 1 || end.Operands.size() == 2,
              "invalid checked operand count");
      if (end.Conversion) {
        Require(end.Operands.size() == 1 &&
                    Target::IsNumeric(type(end.Operands[0])) &&
                    Target::IsNumeric(end.ResultType),
                "invalid numeric conversion");
      } else if (end.Operands.size() == 1) {
        Require(UnaryTypes(end.Operation, type(end.Operands[0]), end.ResultType,
                           true),
                "invalid checked unary operation");
      } else {
        Require(OperandTypes(end.Operation, type(end.Operands[0]),
                             type(end.Operands[1]), end.ResultType, true),
                "invalid checked binary operation");
      }
      edge(end.First, end.ResultType);
      edge(end.Second, Type::U32);
      break;
    case Exit::Return:
      Require(end.Operands.size() == 1 && function.ReturnType != Type::Never &&
                  type(end.Operands[0]) == function.ReturnType,
              "return type mismatch");
      break;
    case Exit::Fail:
      Require(end.Operands.size() == 1 && type(end.Operands[0]) == Type::U32 &&
                  end.Location.Line != 0 && end.Location.Column != 0,
              "invalid failure record");
      break;
    case Exit::Abort:
    case Exit::Unreachable:
      Require(end.Operands.empty(), "terminal operation has operands");
      break;
    default:
      Require(false, "unknown terminator kind");
      break;
    }
  }
  for (const auto &definition : definitions) {
    Require(definition.first != None, "value has no definition");
  }
  // A usize is a representation, not a bounds proof. Dynamic projections
  // accept only the normalized result of their own array's successful check.
  // Keep this edge private so another predecessor cannot inject an arbitrary
  // index into a block parameter that happens to have the right type.
  for (const auto &place : function.Places) {
    if (place.Form != Projection::DynamicElement) {
      continue;
    }
    const auto [success, order] = definitions[place.IndexValue];
    Require(order == 0 && success != 0 &&
                function.Blocks[success].Parameters.size() == 1 &&
                predecessors[success].size() == 1,
            "dynamic index has no private bounds-success edge");
    const auto &check = function.Blocks[predecessors[success][0]].End;
    Require(check.Kind == Exit::Bounds && check.First.Target == success &&
                check.Place == place.Parent,
            "dynamic index proof belongs to a different array or operation");
  }
  // Dominance is calculated independently of the producer. Even structurally
  // unreachable records are type-checked; only reachable paths carry dataflow.
  const Dominance dominance(function, predecessors);
  for (Id b = 0; b < blocks; ++b) {
    const auto use = [&](Id value, std::size_t order) {
      const auto [owner, definitionOrder] = definitions.at(value);
      Require(owner == b
                  ? definitionOrder < order
                  : !dominance.reachable(b) || dominance.contains(owner, b),
              "use is not dominated by its definition");
    };
    const auto &block = function.Blocks[b];
    for (std::size_t i = 0; i < block.Instructions.size(); ++i) {
      for (const auto operand : block.Instructions[i].Operands) {
        use(operand, i + 1);
      }
      const auto &instruction = block.Instructions[i];
      if (instruction.Code == Opcode::Load ||
          instruction.Code == Opcode::Move ||
          instruction.Code == Opcode::Observe ||
          instruction.Code == Opcode::Store ||
          instruction.Code == Opcode::Protect ||
          instruction.Code == Opcode::Unprotect ||
          instruction.Code == Opcode::Locate) {
        for (auto place = instruction.Entity; place != None;
             place = function.Places[place].Parent) {
          const auto &path = function.Places[place];
          if (path.IndexValue != None) {
            use(path.IndexValue, i + 1);
          }
          if (path.ValueOwner != None) {
            use(path.ValueOwner, i + 1);
          }
        }
      }
    }
    const auto order = block.Instructions.size() + 1;
    if (block.End.Kind == Exit::Bounds || block.End.Kind == Exit::Variant) {
      for (auto place = block.End.Place; place != None;
           place = function.Places[place].Parent) {
        const auto &path = function.Places[place];
        if (path.IndexValue != None) {
          use(path.IndexValue, order);
        }
        if (path.ValueOwner != None) {
          use(path.ValueOwner, order);
        }
      }
    }
    for (const auto operand : block.End.Operands) {
      use(operand, order);
    }
    for (const auto operand : block.End.First.Arguments) {
      use(operand, order);
    }
    for (const auto operand : block.End.Second.Arguments) {
      use(operand, order);
    }
  }
}
} // namespace
auto Verify(Draft draft,
            Diagnostic::Diagnostics &diagnostics) -> std::optional<Program> {
  try {
    Require(draft.Target.valid(),
            "unsupported or inconsistent target description");
    Require(draft.Types.has_value(), "execution IR has no completed types");
    // Do not retain a caller-owned string_view even when its spelling matches
    // the supported target. The verified program uses the canonical owner.
    draft.Target = Target::WindowsX64;
    Require(draft.Entry < draft.Functions.size(),
            "entry function ID out of range");
    const auto &entry = draft.Functions[draft.Entry];
    Require(entry.Name == "main" && entry.ReturnType == Type::I32 &&
                entry.Parameters.empty() && entry.HasNormalEntry,
            "invalid native entry signature");
    for (const auto &function : draft.Functions) {
      VerifyFunction(draft, function);
    }
    return Program(std::move(draft));
  } catch (const std::logic_error &error) {
    diagnostics.error("IR verification", error.what());
    return std::nullopt;
  }
}
} // namespace Chtholly::IR
