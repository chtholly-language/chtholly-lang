// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/Lower/lower.h>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace Chtholly::Lower {
namespace {
using Id = IR::Id;
using Type = Types::TypeId;
using O = Numeric::Operation;
class Builder {
public:
  explicit Builder(const SemIR::Storage &source) : Source(source) {
    Result.Target = source.Target;
    Result.SourceName = source.SourceName;
    Result.Entry = source.Entry;
    Result.Types = source.Types;
    for (const auto &function : source.Functions) {
      IR::Function lowered;
      lowered.Name = function.Name;
      lowered.ReturnType = function.ReturnType;
      lowered.HasNormalEntry = function.HasNormalEntry;
      for (const auto &parameter : function.Parameters) {
        lowered.Parameters.push_back({parameter.ValueType, parameter.Local});
      }
      for (const auto &local : function.Locals) {
        lowered.Locals.push_back(local.ValueType);
        lowered.LocalWritable.push_back(local.Writable);
        const auto root = static_cast<Id>(lowered.Places.size());
        IR::Place place;
        place.Local = static_cast<Id>(lowered.LocalRoots.size());
        place.ValueType = local.ValueType;
        place.Writable = local.Writable;
        lowered.LocalRoots.push_back(root);
        lowered.Places.push_back(place);
      }
      Result.Functions.push_back(std::move(lowered));
    }
  }
  auto run() -> IR::Draft {
    for (Id i = 0; i < Result.Functions.size(); ++i) {
      Function = &Result.Functions[i];
      if (!Function->HasNormalEntry) {
        continue;
      }
      Current = makeBlock();
      ScopeLocals.clear();
      ScopeLocals.emplace_back();
      TemporaryPlaces.clear();
      Aliases.clear();
      ActiveProtections.clear();
      for (const auto &parameter : Function->Parameters) {
        const auto value = makeValue(parameter.ValueType);
        Function->Blocks[Current].Parameters.push_back(value);
        live(Function->LocalRoots[parameter.Local]);
        storePlace(Function->LocalRoots[parameter.Local], value);
        ScopeLocals.back().push_back(Function->LocalRoots[parameter.Local]);
      }
      expression(Source.Functions[i].Body);
      if (Current != IR::None) {
        // The body block ended its own locals. Parameters belong to the outer
        // function scope and must also end on the implicit unit return path.
        endScopes(0);
        endTemporaries(0);
        IR::Terminator end;
        end.Kind = IR::Exit::Return;
        end.Operands = {constant(Numeric::Value::Unit())};
        terminate(std::move(end));
      }
    }
    return std::move(Result);
  }

private:
  auto makeValue(Type type) -> Id {
    const auto id = static_cast<Id>(Function->Values.size());
    Function->Values.push_back(type);
    Function->ValuePlaces.push_back(IR::None);
    if (type.owner() != nullptr) {
      IR::Place place;
      place.ValueType = type;
      place.ValueOwner = id;
      const auto root = static_cast<Id>(Function->Places.size());
      Function->Places.push_back(place);
      Function->ValuePlaces[id] = root;
      TemporaryPlaces.push_back(root);
    }
    return id;
  }
  auto makeBlock(Type parameter = Type::Invalid) -> Id {
    const auto id = static_cast<Id>(Function->Blocks.size());
    Function->Blocks.emplace_back();
    if (parameter != Type::Invalid) {
      Function->Blocks[id].Parameters.push_back(makeValue(parameter));
    }
    return id;
  }
  auto parameter(Id block) const -> Id {
    return Function->Blocks[block].Parameters.at(0);
  }
  auto emit(IR::Instruction instruction, Type type) -> Id {
    if (Current == IR::None) {
      return IR::None;
    }
    if (type != Type::Invalid && Result.Types->inhabited(type)) {
      instruction.Result = makeValue(type);
    }
    const auto result = instruction.Result;
    instruction.SourceOffset = CurrentSourceOffset;
    Function->Blocks[Current].Instructions.push_back(std::move(instruction));
    return result;
  }
  auto constant(Numeric::Value value) -> Id {
    IR::Instruction instruction;
    instruction.Code = IR::Opcode::Constant;
    const auto type = Target::SourceType(value.type());
    instruction.Constant = std::move(value);
    return emit(std::move(instruction), type);
  }
  auto storePlace(Id place, Id value) -> void {
    IR::Instruction instruction;
    instruction.Code = IR::Opcode::Store;
    instruction.Entity = place;
    instruction.Operands = {value};
    emit(std::move(instruction), Type::Invalid);
  }
  auto event(IR::Opcode code, Id place) -> void {
    IR::Instruction instruction;
    instruction.Code = code;
    instruction.Entity = place;
    emit(std::move(instruction), Type::Invalid);
  }
  auto live(Id place) -> void { event(IR::Opcode::Live, place); }
  auto endScopes(std::size_t depth) -> void {
    for (auto i = ScopeLocals.size(); i > depth; --i) {
      for (auto local = ScopeLocals[i - 1].rbegin();
           local != ScopeLocals[i - 1].rend(); ++local) {
        if (local->Group == IR::None) {
          event(IR::Opcode::Dead, local->Place);
        } else {
          IR::Instruction end;
          end.Code = IR::Opcode::EndBindings;
          end.Entity = local->Group;
          end.Operands = {local->Selection};
          emit(std::move(end), Type::Invalid);
        }
      }
    }
  }
  auto endTemporaries(std::size_t start, Id preserve = IR::None) -> void {
    for (auto i = TemporaryPlaces.size(); i > start; --i) {
      if (TemporaryPlaces[i - 1] != preserve) {
        event(IR::Opcode::EndValue, TemporaryPlaces[i - 1]);
      }
    }
  }
  auto fieldPlace(Id parent, std::uint64_t field, Id variant = IR::None) -> Id {
    const auto parentType = Function->Places.at(parent).ValueType;
    const auto &type = Result.Types->get(parentType);
    const auto valueType =
        type.Form == Types::Kind::Array ? type.Element
        : variant != IR::None
            ? type.Variants.at(variant)
                  .Fields.at(static_cast<std::size_t>(field))
                  .Type
            : type.Fields.at(static_cast<std::size_t>(field)).Type;
    const auto form = type.Form == Types::Kind::Array ? IR::Projection::Element
                      : variant != IR::None ? IR::Projection::VariantField
                                            : IR::Projection::Field;
    for (Id i = 0; i < Function->Places.size(); ++i) {
      const auto &place = Function->Places[i];
      if (place.Parent == parent && place.Form == form &&
          place.Index == field && place.Variant == variant) {
        return i;
      }
    }
    IR::Place place;
    place.Parent = parent;
    place.Form = form;
    place.Index = field;
    place.Variant = variant;
    place.ValueType = valueType;
    place.Writable = Function->Places[parent].Writable;
    const auto id = static_cast<Id>(Function->Places.size());
    Function->Places.push_back(place);
    return id;
  }
  auto materialize(Id value) -> Id {
    if (Function->ValuePlaces.at(value) != IR::None) {
      return Function->ValuePlaces[value];
    }
    IR::Place place;
    place.Local = static_cast<Id>(Function->Locals.size());
    place.ValueType = Function->Values[value];
    const auto id = static_cast<Id>(Function->Places.size());
    Function->Locals.push_back(place.ValueType);
    Function->LocalWritable.push_back(false);
    Function->LocalRoots.push_back(id);
    Function->Places.push_back(place);
    TemporaryPlaces.push_back(id);
    live(id);
    storePlace(id, value);
    return id;
  }
  auto place(Id id) -> Id {
    const auto &node = Source.Nodes.at(id);
    if (node.Form == SemIR::Kind::Group) {
      return place(node.Left);
    }
    if (node.Form == SemIR::Kind::Local) {
      if (const auto found = Aliases.find(node.Entity);
          found != Aliases.end()) {
        return found->second;
      }
      return Function->LocalRoots.at(node.Entity);
    }
    if (node.Form == SemIR::Kind::Project) {
      const auto base = place(node.Left);
      return Current == IR::None ? IR::None : fieldPlace(base, node.Entity);
    }
    if (node.Form == SemIR::Kind::Index) {
      const auto base = place(node.Left);
      if (Current == IR::None) {
        return IR::None;
      }
      event(IR::Opcode::Protect, base);
      ActiveProtections.push_back(base);
      const auto index = expression(node.Right);
      ActiveProtections.pop_back();
      if (Current == IR::None) {
        return IR::None;
      }
      event(IR::Opcode::Unprotect, base);
      if (node.FixedIndex) {
        return fieldPlace(base, *node.FixedIndex);
      }
      const auto success = makeBlock(Type::USize);
      const auto failure = makeBlock(Type::U32);
      IR::Terminator bounds;
      bounds.Kind = IR::Exit::Bounds;
      bounds.Operands = {index};
      bounds.Place = base;
      bounds.Count = node.Count;
      bounds.First.Target = success;
      bounds.Second.Target = failure;
      bounds.Location = node.Location;
      terminate(std::move(bounds));
      failureBlock(failure, node.Location);
      Current = success;
      IR::Place result;
      result.Form = IR::Projection::DynamicElement;
      result.Parent = base;
      result.IndexValue = parameter(success);
      result.ValueType = node.ValueType;
      result.Writable = Function->Places[base].Writable;
      const auto selected = static_cast<Id>(Function->Places.size());
      Function->Places.push_back(result);
      return selected;
    }
    const auto value = expression(id);
    return Current == IR::None ? IR::None : materialize(value);
  }
  auto releaseProtections(std::size_t depth) -> void {
    for (auto i = ActiveProtections.size(); i > depth; --i) {
      event(IR::Opcode::Unprotect, ActiveProtections[i - 1]);
    }
  }
  auto acquire(Id source, Type type, bool forceMove = false,
               bool observe = false) -> Id {
    IR::Instruction instruction;
    instruction.Code = observe ? IR::Opcode::Observe
                       : forceMove || !Result.Types->copy(type)
                           ? IR::Opcode::Move
                           : IR::Opcode::Load;
    instruction.Entity = source;
    return emit(std::move(instruction), type);
  }
  auto terminate(IR::Terminator end) -> void {
    if (Current == IR::None) {
      return;
    }
    end.SourceOffset = CurrentSourceOffset;
    Function->Blocks[Current].End = std::move(end);
    Current = IR::None;
  }
  auto jump(Id destination, std::vector<Id> arguments = {}) -> void {
    IR::Terminator end;
    end.Kind = IR::Exit::Jump;
    end.First = {destination, std::move(arguments)};
    terminate(std::move(end));
  }
  auto failureBlock(Id block, Source::Location location) -> void {
    Current = block;
    IR::Terminator end;
    end.Kind = IR::Exit::Fail;
    end.Operands = {parameter(block)};
    end.Location = location;
    terminate(std::move(end));
  }
  auto checked(const SemIR::Node &node, std::vector<Id> operands) -> Id {
    const auto success = makeBlock(node.ValueType);
    const auto failure = Recovery == IR::None ? makeBlock(Type::U32) : Recovery;
    IR::Terminator end;
    end.Kind = IR::Exit::Checked;
    end.Operands = std::move(operands);
    end.First.Target = success;
    end.Second.Target = failure;
    end.Operation = node.Operation;
    end.Conversion = node.Form == SemIR::Kind::Convert;
    end.ResultType = node.ValueType;
    end.Location = node.Location;
    terminate(std::move(end));
    if (Recovery == IR::None) {
      failureBlock(failure, node.Location);
    }
    Current = success;
    return parameter(success);
  }
  auto branch(const SemIR::Node &node) -> Id {
    const auto condition = expression(node.Left);
    if (Current == IR::None) {
      return IR::None;
    }
    if (Source.Nodes[node.Left].Constant) {
      if (Source.Nodes[node.Left].Constant->boolean()) {
        return expression(node.Right);
      }
      return node.Alternative == SemIR::None ? constant(Numeric::Value::Unit())
                                             : expression(node.Alternative);
    }
    const auto thenBlock = makeBlock();
    const auto elseBlock = makeBlock();
    const auto merge = node.Completes ? makeBlock(node.ValueType) : IR::None;
    IR::Terminator end;
    end.Kind = IR::Exit::Branch;
    end.Operands = {condition};
    end.First.Target = thenBlock;
    end.Second.Target = elseBlock;
    terminate(std::move(end));
    Current = thenBlock;
    const auto thenValue = expression(node.Right);
    if (Current != IR::None) {
      if (merge == IR::None) {
        IR::Terminator unreachable;
        unreachable.Kind = IR::Exit::Unreachable;
        terminate(unreachable);
      } else {
        jump(merge, {thenValue});
      }
    }
    Current = elseBlock;
    const auto elseValue = node.Alternative == SemIR::None
                               ? constant(Numeric::Value::Unit())
                               : expression(node.Alternative);
    if (Current != IR::None) {
      if (merge == IR::None) {
        IR::Terminator unreachable;
        unreachable.Kind = IR::Exit::Unreachable;
        terminate(unreachable);
      } else {
        jump(merge, {elseValue});
      }
    }
    Current = merge;
    return merge == IR::None ? IR::None : parameter(merge);
  }
  auto logical(const SemIR::Node &node) -> Id {
    const auto left = expression(node.Left);
    if (Current == IR::None) {
      return IR::None;
    }
    if (Source.Nodes[node.Left].Constant) {
      const auto condition = Source.Nodes[node.Left].Constant->boolean();
      if ((node.Operation == O::LogicalAnd && !condition) ||
          (node.Operation == O::LogicalOr && condition)) {
        return left;
      }
      return expression(node.Right);
    }
    const auto evaluate = makeBlock();
    const auto shortCircuit = makeBlock();
    const auto merge = makeBlock(Type::Bool);
    IR::Terminator end;
    end.Kind = IR::Exit::Branch;
    end.Operands = {left};
    end.First.Target =
        node.Operation == O::LogicalAnd ? evaluate : shortCircuit;
    end.Second.Target =
        node.Operation == O::LogicalAnd ? shortCircuit : evaluate;
    terminate(std::move(end));
    Current = shortCircuit;
    const auto shortValue =
        constant(Numeric::Value::Boolean(node.Operation == O::LogicalOr));
    jump(merge, {shortValue});
    Current = evaluate;
    const auto right = expression(node.Right);
    if (Current != IR::None) {
      jump(merge, {right});
    }
    Current = merge;
    return parameter(merge);
  }
  auto binding(const SemIR::Node &node) -> Id {
    if (node.Alternative == SemIR::None) {
      const auto value = expression(node.Left);
      if (Current != IR::None) {
        destructure(node.Pattern, materialize(value));
      }
      return Current == IR::None ? IR::None : constant(Numeric::Value::Unit());
    }
    auto root = node.Left;
    while (Source.Nodes[root].Form == SemIR::Kind::Group) {
      root = Source.Nodes[root].Left;
    }
    if (!Source.Nodes[root].Completes) {
      // A divergent operand never reaches the checked root. There is neither
      // a success edge nor a recovery edge to manufacture in this case.
      return expression(node.Left);
    }
    if (Source.Nodes[root].Constant) {
      const auto value = expression(node.Left);
      if (Current != IR::None) {
        destructure(node.Pattern, materialize(value));
      }
      return Current == IR::None ? IR::None : constant(Numeric::Value::Unit());
    }
    const auto recovery = makeBlock(Type::U32);
    const auto savedRoot = RecoveryNode;
    const auto savedRecovery = Recovery;
    RecoveryNode = root;
    Recovery = recovery;
    const auto value = expression(node.Left);
    RecoveryNode = savedRoot;
    Recovery = savedRecovery;
    const auto continuation = node.Completes
                                  ? makeBlock(Source.Nodes[node.Left].ValueType)
                                  : IR::None;
    if (Current != IR::None) {
      jump(continuation, {value});
    }
    Current = recovery;
    const auto replacement = expression(node.Alternative);
    if (Current != IR::None) {
      jump(continuation, {replacement});
    }
    Current = continuation;
    if (Current == IR::None) {
      return IR::None;
    }
    destructure(node.Pattern, materialize(parameter(continuation)));
    return constant(Numeric::Value::Unit());
  }
  struct PatternTest {
    Id Place;
    Id Variant = IR::None;
    std::optional<Numeric::Value> Literal;
  };
  struct PatternPlan {
    std::vector<PatternTest> Tests;
    std::vector<std::pair<Id, Id>> Bindings;
  };
  auto patternPlans(Id id, Id source) -> std::vector<PatternPlan> {
    const auto &pattern = Source.Patterns.at(id);
    if (pattern.Form == SemIR::PatternKind::Or) {
      std::vector<PatternPlan> result;
      for (const auto child : pattern.Children) {
        auto plans = patternPlans(child, source);
        for (auto &plan : plans) {
          result.push_back(std::move(plan));
        }
        if (result.size() > 100000) {
          throw std::logic_error("pattern lowering resource budget exhausted");
        }
      }
      return result;
    }
    PatternPlan initial;
    if (pattern.Form == SemIR::PatternKind::Binding) {
      initial.Bindings.push_back({pattern.Local, source});
    }
    if (pattern.Form == SemIR::PatternKind::Boolean ||
        pattern.Form == SemIR::PatternKind::Integer) {
      initial.Tests.push_back({source, IR::None, pattern.Literal});
    }
    if (pattern.Form == SemIR::PatternKind::Variant) {
      initial.Tests.push_back({source, pattern.Variant, std::nullopt});
    }
    std::vector<PatternPlan> result{std::move(initial)};
    for (std::size_t i = 0; i < pattern.Children.size(); ++i) {
      const auto childSource = fieldPlace(
          source, pattern.Fields.at(i),
          pattern.Form == SemIR::PatternKind::Variant ? pattern.Variant
                                                      : IR::None);
      auto children = patternPlans(pattern.Children[i], childSource);
      std::vector<PatternPlan> expanded;
      for (const auto &prefix : result) {
        for (const auto &child : children) {
          auto combined = prefix;
          combined.Tests.insert(combined.Tests.end(), child.Tests.begin(),
                                child.Tests.end());
          combined.Bindings.insert(combined.Bindings.end(),
                                   child.Bindings.begin(),
                                   child.Bindings.end());
          expanded.push_back(std::move(combined));
          if (expanded.size() > 100000) {
            throw std::logic_error(
                "pattern lowering resource budget exhausted");
          }
        }
      }
      result = std::move(expanded);
    }
    return result;
  }
  auto testPattern(const PatternPlan &plan, Id success, Id failure) -> void {
    for (const auto &test : plan.Tests) {
      const auto next = makeBlock();
      IR::Terminator end;
      end.First.Target = next;
      end.Second.Target = failure;
      if (test.Variant != IR::None) {
        end.Kind = IR::Exit::Variant;
        end.Place = test.Place;
        end.Variant = test.Variant;
      } else {
        const auto value = acquire(
            test.Place, Function->Places[test.Place].ValueType, false, true);
        IR::Instruction comparison;
        comparison.Code = IR::Opcode::Binary;
        comparison.Operation = O::Equal;
        comparison.Operands = {value, constant(*test.Literal)};
        end.Kind = IR::Exit::Branch;
        end.Operands = {emit(std::move(comparison), Type::Bool)};
      }
      terminate(std::move(end));
      Current = next;
    }
    jump(success);
  }
  auto commit(const PatternPlan &plan, bool registerScope = true) -> void {
    for (const auto [local, source] : plan.Bindings) {
      const auto value = acquire(source, Function->Locals[local]);
      const auto target = Function->LocalRoots[local];
      live(target);
      storePlace(target, value);
      if (registerScope) {
        ScopeLocals.back().push_back(target);
      }
    }
  }
  auto destructure(Id pattern, Id source) -> void {
    const auto plans = patternPlans(pattern, source);
    if (plans.size() == 1) {
      commit(plans[0]);
      return;
    }
    const auto merge = makeBlock(Type::U32);
    const auto group = static_cast<Id>(Function->BindingGroups.size());
    Function->BindingGroups.emplace_back();
    auto next = Current;
    for (std::size_t i = 0; i < plans.size(); ++i) {
      const auto &plan = plans[i];
      Current = next;
      const auto selected = makeBlock();
      next = makeBlock();
      testPattern(plan, selected, next);
      Current = selected;
      commit(plan, false);
      std::vector<Id> order;
      for (const auto &[local, projection] : plan.Bindings) {
        static_cast<void>(projection);
        order.push_back(Function->LocalRoots[local]);
      }
      Function->BindingGroups[group].CreationOrders.push_back(std::move(order));
      jump(merge, {constant(Numeric::Value::FromBits(Type::U32, {i, 0}))});
    }
    Current = next;
    IR::Terminator unreachable;
    unreachable.Kind = IR::Exit::Unreachable;
    terminate(unreachable);
    Current = merge;
    Cleanup cleanup(IR::None);
    cleanup.Group = group;
    cleanup.Selection = parameter(merge);
    ScopeLocals.back().push_back(cleanup);
  }
  auto matching(const SemIR::Node &node) -> Id {
    const auto input = expression(node.Left);
    if (Current == IR::None) {
      return IR::None;
    }
    const auto source = materialize(input);
    const auto merge = node.Completes ? makeBlock(node.ValueType) : IR::None;
    auto nextArm = Current;
    for (const auto &arm : node.Arms) {
      if (!arm.Reachable) {
        continue;
      }
      const auto plans = patternPlans(arm.Pattern, source);
      Current = nextArm;
      nextArm = makeBlock();
      auto nextAlternative = Current;
      for (const auto &plan : plans) {
        Current = nextAlternative;
        const auto selected = makeBlock();
        nextAlternative = makeBlock();
        testPattern(plan, selected, nextAlternative);
        Current = selected;
        ScopeLocals.emplace_back();
        const auto oldAliases = Aliases;
        for (const auto [local, projection] : plan.Bindings) {
          Aliases[local] = projection;
        }
        if (arm.Guard != SemIR::None) {
          const auto condition = expression(arm.Guard);
          if (Current != IR::None) {
            if (!arm.BodyReachable) {
              jump(nextArm);
            } else {
              const auto body = makeBlock();
              IR::Terminator guard;
              guard.Kind = IR::Exit::Branch;
              guard.Operands = {condition};
              guard.First.Target = body;
              guard.Second.Target = nextArm;
              terminate(std::move(guard));
              Current = body;
            }
          }
        }
        Aliases = oldAliases;
        if (Current != IR::None) {
          commit(plan);
          const auto value = expression(arm.Body);
          if (Current != IR::None) {
            endScopes(ScopeLocals.size() - 1);
            if (merge != IR::None) {
              jump(merge, {value});
            } else {
              IR::Terminator end;
              end.Kind = IR::Exit::Unreachable;
              terminate(end);
            }
          }
        }
        ScopeLocals.pop_back();
      }
      Current = nextAlternative;
      jump(nextArm);
    }
    Current = nextArm;
    IR::Terminator impossible;
    impossible.Kind = IR::Exit::Unreachable;
    terminate(impossible);
    Current = merge;
    return merge == IR::None ? IR::None : parameter(merge);
  }
  struct LoopTarget {
    Id Node;
    Id Header;
    Id Exit;
    std::size_t ScopeDepth;
    std::size_t TemporaryStart;
    bool While;
    std::size_t ProtectionDepth;
  };
  auto looping(Id id, const SemIR::Node &node) -> Id {
    const bool isWhile = node.Form == SemIR::Kind::While;
    const auto header = makeBlock();
    const auto body = makeBlock();
    const auto exit = node.Completes
                          ? makeBlock(isWhile ? Type::Invalid : node.ValueType)
                          : makeBlock();
    jump(header);
    LoopTargets.push_back({id, header, exit, ScopeLocals.size(),
                           TemporaryPlaces.size(), isWhile,
                           ActiveProtections.size()});
    Current = header;
    if (isWhile) {
      const auto conditionTemporaries = TemporaryPlaces.size();
      const auto condition = expression(node.Left);
      if (Current != IR::None) {
        endTemporaries(conditionTemporaries);
        if (Source.Nodes[node.Left].Constant &&
            !Source.Nodes[node.Left].Constant->boolean()) {
          jump(exit);
        } else {
          IR::Terminator end;
          end.Kind = IR::Exit::Branch;
          end.Operands = {condition};
          end.First.Target = body;
          end.Second.Target = exit;
          terminate(std::move(end));
        }
      }
    } else {
      jump(body);
    }
    Current = body;
    const bool bodyHasPath =
        !isWhile || (Source.Nodes[node.Left].Completes &&
                     (!Source.Nodes[node.Left].Constant ||
                      Source.Nodes[node.Left].Constant->boolean()));
    if (bodyHasPath) {
      expression(node.Right);
    } else {
      IR::Terminator unreachable;
      unreachable.Kind = IR::Exit::Unreachable;
      terminate(unreachable);
    }
    if (Current != IR::None) {
      jump(header);
    }
    LoopTargets.pop_back();
    Current = exit;
    if (!node.Completes) {
      IR::Terminator end;
      end.Kind = IR::Exit::Unreachable;
      terminate(end);
      return IR::None;
    }
    return isWhile ? constant(Numeric::Value::Unit()) : parameter(exit);
  }
  auto call(const SemIR::Node &node) -> Id {
    std::vector<Id> arguments(node.Arguments.size(), IR::None);
    for (std::size_t i = 0; i < node.Arguments.size(); ++i) {
      const auto value = expression(node.Arguments[i]);
      if (Current == IR::None) {
        return IR::None;
      }
      arguments.at(node.Slots[i]) = value;
    }
    if (node.Intrinsic != SemIR::Builtin::None) {
      if (node.Intrinsic == SemIR::Builtin::Assert) {
        const auto success = makeBlock();
        const auto failure = makeBlock();
        IR::Terminator end;
        end.Kind = IR::Exit::Branch;
        end.Operands = {arguments.at(0)};
        end.First.Target = success;
        end.Second.Target = failure;
        terminate(std::move(end));
        Current = failure;
        IR::Terminator failed;
        failed.Kind = IR::Exit::Fail;
        failed.Location = node.Location;
        failed.Operands = {
            constant(Numeric::Value::FromBits(Type::U32, {21, 0}))};
        terminate(std::move(failed));
        Current = success;
        return constant(Numeric::Value::Unit());
      }
      IR::Terminator end;
      end.Kind = node.Intrinsic == SemIR::Builtin::Abort ? IR::Exit::Abort
                                                         : IR::Exit::Fail;
      end.Location = node.Location;
      if (end.Kind == IR::Exit::Fail) {
        end.Operands = {constant(Numeric::Value::FromBits(Type::U32, {20, 0}))};
      }
      terminate(std::move(end));
      return IR::None;
    }
    IR::Instruction instruction;
    instruction.Code = IR::Opcode::Call;
    instruction.Entity = node.Entity;
    instruction.Operands = std::move(arguments);
    const auto result = emit(std::move(instruction), node.ValueType);
    if (!Result.Types->inhabited(node.ValueType)) {
      IR::Terminator unreachable;
      unreachable.Kind = IR::Exit::Unreachable;
      terminate(unreachable);
    }
    return result;
  }
  auto expression(Id id) -> Id {
    if (Current == IR::None) {
      return IR::None;
    }
    const auto &node = Source.Nodes.at(id);
    const auto previousOffset = CurrentSourceOffset;
    CurrentSourceOffset = node.Span.Begin;
    struct OffsetGuard {
      std::size_t &Offset;
      std::size_t Previous;
      ~OffsetGuard() { Offset = Previous; }
    } offsetGuard{CurrentSourceOffset, previousOffset};
    if (node.Constant && node.Completes) {
      return constant(*node.Constant);
    }
    if (node.Form != SemIR::Kind::Group &&
        Numeric::IsRecoverable(node.ConstantFailure)) {
      if (id == RecoveryNode) {
        const auto error = constant(Numeric::Value::FromBits(
            Type::U32, {static_cast<unsigned>(node.ConstantFailure), 0}));
        jump(Recovery, {error});
        return IR::None;
      }
      throw std::logic_error("unhandled constant failure reached Lower");
    }
    switch (node.Form) {
    case SemIR::Kind::Literal:
      throw std::logic_error("literal without a checked value");
    case SemIR::Kind::Group:
      return expression(node.Left);
    case SemIR::Kind::Local: {
      return acquire(place(id), node.ValueType, false,
                     node.ValueCategory == SemIR::Category::TrialPlace);
    }
    case SemIR::Kind::Project:
    case SemIR::Kind::Index: {
      const auto source = place(id);
      return Current == IR::None
                 ? IR::None
                 : acquire(source, node.ValueType, false,
                           node.ValueCategory == SemIR::Category::TrialPlace);
    }
    case SemIR::Kind::Copy:
    case SemIR::Kind::Move:
      if (Source.Nodes[node.Left].ValueCategory != SemIR::Category::Value) {
        const auto source = place(node.Left);
        return Current == IR::None ? IR::None
                                   : acquire(source, node.ValueType,
                                             node.Form == SemIR::Kind::Move);
      }
      return expression(node.Left);
    case SemIR::Kind::Length: {
      const auto source = place(node.Left);
      if (Current == IR::None) {
        return IR::None;
      }
      event(IR::Opcode::Locate, source);
      return constant(Numeric::Value::FromBits(Type::USize, {node.Count, 0}));
    }
    case SemIR::Kind::Construct: {
      IR::Instruction instruction;
      instruction.Code =
          node.Repeat ? IR::Opcode::Repeat : IR::Opcode::Construct;
      instruction.Entity = node.Entity;
      instruction.Count = node.Count;
      instruction.Slots = node.Slots;
      for (const auto element : node.Statements) {
        instruction.Operands.push_back(expression(element));
        if (Current == IR::None) {
          return IR::None;
        }
      }
      return emit(std::move(instruction), node.ValueType);
    }
    case SemIR::Kind::Match:
      return matching(node);
    case SemIR::Kind::While:
    case SemIR::Kind::Loop:
      return looping(id, node);
    case SemIR::Kind::Break:
    case SemIR::Kind::Continue: {
      const auto target = std::find_if(
          LoopTargets.rbegin(), LoopTargets.rend(),
          [&](const LoopTarget &loop) { return loop.Node == node.Entity; });
      if (target == LoopTargets.rend()) {
        throw std::logic_error("checked loop target is not active");
      }
      const auto loop = *target;
      const auto value = node.Left == SemIR::None
                             ? constant(Numeric::Value::Unit())
                             : expression(node.Left);
      if (Current != IR::None) {
        const auto preserve =
            value == IR::None ? IR::None : Function->ValuePlaces[value];
        releaseProtections(loop.ProtectionDepth);
        endScopes(loop.ScopeDepth);
        endTemporaries(loop.TemporaryStart, preserve);
        if (node.Form == SemIR::Kind::Continue) {
          jump(loop.Header);
        } else {
          jump(loop.Exit,
               loop.While ? std::vector<Id>{} : std::vector<Id>{value});
        }
      }
      return IR::None;
    }
    case SemIR::Kind::Bind:
      return binding(node);
    case SemIR::Kind::Assign: {
      const auto value = expression(node.Right);
      if (Current != IR::None) {
        const auto target = place(node.Left);
        if (Current != IR::None) {
          storePlace(target, value);
        }
      }
      return Current == IR::None ? IR::None : constant(Numeric::Value::Unit());
    }
    case SemIR::Kind::Discard:
      expression(node.Left);
      return Current == IR::None ? IR::None : constant(Numeric::Value::Unit());
    case SemIR::Kind::Return: {
      const auto value = node.Left == SemIR::None
                             ? constant(Numeric::Value::Unit())
                             : expression(node.Left);
      if (Current != IR::None) {
        releaseProtections(0);
        endScopes(0);
        endTemporaries(0, Function->ValuePlaces[value]);
        IR::Terminator end;
        end.Kind = IR::Exit::Return;
        end.Operands = {value};
        terminate(std::move(end));
      }
      return IR::None;
    }
    case SemIR::Kind::Block: {
      ScopeLocals.emplace_back();
      for (const auto statement : node.Statements) {
        const auto start = TemporaryPlaces.size();
        expression(statement);
        if (Current != IR::None) {
          endTemporaries(start);
        }
      }
      if (Current == IR::None) {
        ScopeLocals.pop_back();
        return IR::None;
      }
      const auto value = node.Left == SemIR::None
                             ? constant(Numeric::Value::Unit())
                             : expression(node.Left);
      if (Current != IR::None) {
        endScopes(ScopeLocals.size() - 1);
      }
      ScopeLocals.pop_back();
      return value;
    }
    case SemIR::Kind::If:
      return branch(node);
    case SemIR::Kind::Call:
      return call(node);
    case SemIR::Kind::Binary:
      if (node.Operation == O::LogicalAnd || node.Operation == O::LogicalOr) {
        return logical(node);
      }
      [[fallthrough]];
    case SemIR::Kind::Unary:
    case SemIR::Kind::Convert: {
      std::vector<Id> operands;
      operands.push_back(expression(node.Left));
      if (node.Form == SemIR::Kind::Binary && Current != IR::None) {
        operands.push_back(expression(node.Right));
      }
      if (Current == IR::None) {
        return IR::None;
      }
      const auto savedRecovery = Recovery;
      if (id != RecoveryNode) {
        Recovery = IR::None;
      }
      const bool guarded =
          node.Form == SemIR::Kind::Convert ||
          (Target::IsInteger(node.ValueType) &&
           (node.Operation == O::Add || node.Operation == O::Subtract ||
            node.Operation == O::Multiply || node.Operation == O::Divide ||
            node.Operation == O::Remainder || node.Operation == O::Negate ||
            node.Operation == O::ShiftLeft || node.Operation == O::ShiftRight));
      Id value;
      if (guarded) {
        value = checked(node, std::move(operands));
      } else {
        IR::Instruction instruction;
        instruction.Code = node.Form == SemIR::Kind::Unary ? IR::Opcode::Unary
                                                           : IR::Opcode::Binary;
        instruction.Operation = node.Operation;
        instruction.Operands = std::move(operands);
        value = emit(std::move(instruction), node.ValueType);
      }
      Recovery = savedRecovery;
      return value;
    }
    }
    throw std::logic_error("unknown semantic node");
  }
  const SemIR::Storage &Source;
  IR::Draft Result;
  IR::Function *Function = nullptr;
  Id Current = IR::None;
  Id Recovery = IR::None;
  Id RecoveryNode = IR::None;
  struct Cleanup {
    Cleanup(Id place) : Place(place) {}
    Id Place;
    Id Group = IR::None;
    Id Selection = IR::None;
  };
  std::vector<std::vector<Cleanup>> ScopeLocals;
  std::vector<Id> TemporaryPlaces;
  std::unordered_map<Id, Id> Aliases;
  std::vector<LoopTarget> LoopTargets;
  std::size_t CurrentSourceOffset = 0;
  std::vector<Id> ActiveProtections;
};
} // namespace
auto Lower(const SemIR::Program &program,
           Diagnostic::Diagnostics &diagnostics) -> std::optional<IR::Program> {
  try {
    return IR::Verify(Builder(program.storage()).run(), diagnostics);
  } catch (const std::logic_error &error) {
    diagnostics.error("lower", error.what());
    return std::nullopt;
  }
}
} // namespace Chtholly::Lower
