// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Diagnostic/diagnostic.h>
#include <Chtholly/Flow/flow.h>
#include <algorithm>
#include <deque>
#include <set>
#include <stdexcept>

namespace Chtholly::Flow {
namespace {
using Id = IR::Id;
enum : unsigned char {
  Dead = 1,
  Empty = 2,
  Initialized = 4,
  Moved = 8,
  Partial = 16
};
struct Fact {
  unsigned char State = Dead;
  unsigned char Default = Dead;
  unsigned char Tag = Dead;
  std::set<Id> Variants;
  unsigned ProtectMin = 0;
  unsigned ProtectMax = 0;
  auto operator==(const Fact &) const -> bool = default;
};
using State = std::vector<Fact>;
class SourceFailure : public std::logic_error {
public:
  SourceFailure(std::size_t offset, const char *message)
      : std::logic_error(message), Offset(offset) {}
  std::size_t Offset;
};
class Analysis {
public:
  Analysis(const IR::Draft &program, const IR::Function &function,
           Limits limits)
      : Program(program), Function(function), Boundaries(limits),
        Children(function.Places.size()) {
    if (!function.HasNormalEntry) {
      return;
    }
    const auto places = function.Places.size();
    if (places > limits.Places ||
        (places != 0 && function.Blocks.size() > limits.StateCells / places)) {
      throw std::logic_error(
          "initialization analysis resource budget exhausted (state domain)");
    }
    std::size_t factUnits = places;
    for (const auto &place : function.Places) {
      if (place.ValueType.owner() == nullptr) {
        continue;
      }
      const auto &type = program.Types->get(place.ValueType);
      if (type.Form != Types::Kind::Enum) {
        continue;
      }
      if (type.Variants.size() >
          limits.StateCells - std::min(factUnits, limits.StateCells)) {
        throw std::logic_error("initialization analysis resource budget "
                               "exhausted (variant facts)");
      }
      factUnits += type.Variants.size();
    }
    if (factUnits != 0 &&
        function.Blocks.size() > limits.StateCells / factUnits) {
      throw std::logic_error("initialization analysis resource budget "
                             "exhausted (conditional state domain)");
    }
    for (Id i = 0; i < places; ++i) {
      if (function.Places[i].Parent != IR::None) {
        Children[function.Places[i].Parent].push_back(i);
      }
    }
  }
  auto run() -> void {
    if (!Function.HasNormalEntry) {
      return;
    }
    Incoming.resize(Function.Blocks.size());
    State entry(Function.Places.size());
    for (const auto value : Function.Blocks[0].Parameters) {
      if (Function.ValuePlaces[value] != IR::None) {
        set(entry, Function.ValuePlaces[value], Initialized);
      }
    }
    Incoming[0] = std::move(entry);
    std::deque<Id> work{0};
    std::vector<bool> queued(Function.Blocks.size(), false);
    queued[0] = true;
    const auto queue = [&](Id block) {
      if (!queued[block]) {
        queued[block] = true;
        work.push_back(block);
      }
    };
    while (!work.empty()) {
      const auto block = work.front();
      work.pop_front();
      queued[block] = false;
      auto state = *Incoming[block];
      transfer(block, state, false);
      for (auto &[target, outgoing] : successors(block, state, false)) {
        if (merge(target, outgoing)) {
          queue(target);
        }
      }
    }
    // Diagnostics use the fixed point, not transient worklist guesses.
    for (Id block = 0; block < Function.Blocks.size(); ++block) {
      if (!Incoming[block]) {
        continue;
      }
      auto state = *Incoming[block];
      transfer(block, state, true);
      successors(block, state, true);
    }
  }

private:
  auto require(bool condition, const char *reason, bool report) const -> void {
    if (!condition && report) {
      throw SourceFailure(CurrentOffset, reason);
    }
  }
  auto enumType(Id place) const -> const Types::Record * {
    const auto type = Function.Places[place].ValueType;
    if (type.owner() == nullptr) {
      return nullptr;
    }
    const auto &record = Program.Types->get(type);
    return record.Form == Types::Kind::Enum ? &record : nullptr;
  }
  auto set(State &state, Id root, unsigned char status) const -> void {
    std::vector<Id> work{root};
    while (!work.empty()) {
      const auto place = work.back();
      work.pop_back();
      auto &fact = state[place];
      fact.State = status;
      fact.Default = status;
      fact.Tag = status;
      fact.Variants.clear();
      if (status == Initialized) {
        if (const auto *type = enumType(place)) {
          for (Id i = 0; i < type->Variants.size(); ++i) {
            if (type->Variants[i].Inhabited) {
              fact.Variants.insert(i);
            }
          }
        }
      }
      for (const auto child : Children[place]) {
        work.push_back(child);
      }
    }
  }
  auto recompute(State &state, Id child) const -> void {
    for (auto parent = Function.Places[child].Parent; parent != IR::None;
         parent = Function.Places[parent].Parent) {
      const auto &type = Program.Types->get(Function.Places[parent].ValueType);
      auto &fact = state[parent];
      if ((fact.State & Dead) != 0) {
        continue;
      }
      bool complete = true;
      const auto checkFields = [&](std::uint64_t count, Id variant) {
        std::set<std::uint64_t> represented;
        bool all = true;
        for (const auto item : Children[parent]) {
          const auto &path = Function.Places[item];
          if (path.Form == IR::Projection::DynamicElement ||
              path.Variant != variant) {
            continue;
          }
          represented.insert(path.Index);
          all &= state[item].State == Initialized;
        }
        return all &&
               (represented.size() == count || fact.Default == Initialized);
      };
      if (type.Form == Types::Kind::Enum) {
        complete = fact.Tag == Initialized && !fact.Variants.empty();
        for (const auto variant : fact.Variants) {
          complete &=
              checkFields(type.Variants[variant].Fields.size(), variant);
        }
      } else {
        const auto count =
            type.Form == Types::Kind::Array ? type.Length : type.Fields.size();
        complete = checkFields(count, IR::None);
        // An empty aggregate still needs an explicit token initialization.
        if (count == 0) {
          complete &= fact.Default == Initialized;
        }
      }
      fact.State = complete ? Initialized : Partial;
    }
  }
  auto overlaps(Id left, Id right) const -> bool {
    std::vector<Id> first;
    std::vector<Id> second;
    for (auto id = left; id != IR::None; id = Function.Places[id].Parent) {
      first.push_back(id);
    }
    for (auto id = right; id != IR::None; id = Function.Places[id].Parent) {
      second.push_back(id);
    }
    if (first.back() != second.back()) {
      return false;
    }
    std::reverse(first.begin(), first.end());
    std::reverse(second.begin(), second.end());
    std::size_t i = 0;
    while (i < first.size() && i < second.size() && first[i] == second[i]) {
      ++i;
    }
    if (i == first.size() || i == second.size()) {
      return true;
    }
    const auto &a = Function.Places[first[i]];
    const auto &b = Function.Places[second[i]];
    if (a.Form == IR::Projection::DynamicElement ||
        b.Form == IR::Projection::DynamicElement) {
      return true;
    }
    return a.Index == b.Index && a.Variant == b.Variant;
  }
  auto locate(const State &state, Id place, bool report) const -> void {
    for (auto id = place; id != IR::None; id = Function.Places[id].Parent) {
      const auto &path = Function.Places[id];
      if (path.Parent == IR::None) {
        require((state[id].State & Dead) == 0,
                "position storage is not definitely alive", report);
      }
      if (path.Form == IR::Projection::VariantField) {
        const auto &parent = state[path.Parent];
        require(parent.Tag == Initialized && parent.Variants.size() == 1 &&
                    parent.Variants.contains(path.Variant),
                "enum payload is not proven active", report);
      }
    }
  }
  auto modify(const State &state, Id place, bool report) const -> void {
    locate(state, place, report);
    for (Id other = 0; other < state.size(); ++other) {
      require(state[other].ProtectMax == 0 || !overlaps(place, other),
              "index evaluation invalidates its protected base", report);
    }
  }
  auto read(const State &state, Id place, bool report) const -> void {
    locate(state, place, report);
    requireDynamicBase(state, place, report);
    const auto &path = Function.Places[place];
    const bool complete = path.Form == IR::Projection::DynamicElement
                              ? state[path.Parent].State == Initialized
                              : state[place].State == Initialized;
    require(complete,
            "read requires a fully initialized value (possibly moved or "
            "partially initialized)",
            report);
  }
  auto requireDynamicBase(const State &state, Id place,
                          bool report) const -> void {
    for (auto current = place; current != IR::None;
         current = Function.Places[current].Parent) {
      const auto &projection = Function.Places[current];
      if (projection.Form == IR::Projection::DynamicElement) {
        require(state[projection.Parent].State == Initialized,
                "dynamic element access requires a fully initialized array",
                report);
      }
    }
  }
  auto hasDynamicElement(Id place) const -> bool {
    for (auto current = place; current != IR::None;
         current = Function.Places[current].Parent) {
      if (Function.Places[current].Form == IR::Projection::DynamicElement) {
        return true;
      }
    }
    return false;
  }
  auto dynamicArrayBase(Id place) const -> Id {
    Id result = IR::None;
    for (auto current = place; current != IR::None;
         current = Function.Places[current].Parent) {
      const auto &projection = Function.Places[current];
      if (projection.Form == IR::Projection::DynamicElement) {
        result = projection.Parent;
      }
    }
    return result;
  }
  auto consume(State &state, Id value, bool report) const -> void {
    const auto place = Function.ValuePlaces[value];
    if (place != IR::None) {
      read(state, place, report);
      modify(state, place, report);
      set(state, place, Moved);
    }
  }
  auto transfer(Id block, State &state, bool report) const -> void {
    for (const auto &instruction : Function.Blocks[block].Instructions) {
      CurrentOffset = instruction.SourceOffset;
      const auto place = instruction.Entity;
      const auto resultPlace = instruction.Result == IR::None
                                   ? IR::None
                                   : Function.ValuePlaces[instruction.Result];
      switch (instruction.Code) {
      case IR::Opcode::EndBindings:
        for (const auto root :
             Function.BindingGroups[instruction.Entity].CreationOrders[0]) {
          for (Id other = 0; other < state.size(); ++other) {
            require(state[other].ProtectMax == 0 || !overlaps(root, other),
                    "ending binding invalidates a protected base", report);
          }
          set(state, root, Dead);
        }
        break;
      case IR::Opcode::Live:
        require(state[place].State == Dead,
                "storage is established while an earlier instance remains "
                "alive",
                report);
        set(state, place, Empty);
        break;
      case IR::Opcode::Dead:
      case IR::Opcode::EndValue:
        // Full-expression temporaries can exist on only one predecessor.
        // Ending their remaining responsibility is conditional, not a read.
        for (Id other = 0; other < state.size(); ++other) {
          require(state[other].ProtectMax == 0 || !overlaps(place, other),
                  "ending storage invalidates a protected base", report);
        }
        set(state, place, Dead);
        break;
      case IR::Opcode::Protect:
        locate(state, place, report);
        if (state[place].ProtectMax >= Boundaries.Places) {
          throw std::logic_error("initialization analysis resource budget "
                                 "exhausted (protection depth)");
        }
        ++state[place].ProtectMin;
        ++state[place].ProtectMax;
        break;
      case IR::Opcode::Unprotect:
        require(state[place].ProtectMin != 0, "unbalanced base protection",
                report);
        if (state[place].ProtectMin != 0) {
          --state[place].ProtectMin;
        }
        if (state[place].ProtectMax != 0) {
          --state[place].ProtectMax;
        }
        break;
      case IR::Opcode::Locate:
        locate(state, place, report);
        break;
      case IR::Opcode::Load:
      case IR::Opcode::Observe:
      case IR::Opcode::Move:
        read(state, place, report);
        if (instruction.Code == IR::Opcode::Move) {
          require(!hasDynamicElement(place),
                  "cannot move out of a dynamic array element, including Copy "
                  "elements",
                  report);
          modify(state, place, report);
          set(state, place, Moved);
          recompute(state, place);
        }
        if (resultPlace != IR::None) {
          set(state, resultPlace, Initialized);
        }
        break;
      case IR::Opcode::Store: {
        modify(state, place, report);
        const auto &path = Function.Places[place];
        requireDynamicBase(state, place, report);
        require(state[place].State == Empty || path.Writable,
                "assignment or restoration requires a writable position",
                report);
        if (path.Form == IR::Projection::DynamicElement) {
          require(state[path.Parent].State == Initialized,
                  "dynamic replacement requires a complete array", report);
        }
        consume(state, instruction.Operands[0], report);
        const auto dynamicBase = dynamicArrayBase(place);
        if (dynamicBase == IR::None) {
          set(state, place, Initialized);
          recompute(state, place);
        } else {
          // Every possible element was complete before the replacement. It
          // remains complete, but an unknown index may have changed any
          // observed element's enum tag. Discard those conditional facts as
          // well; keeping a prior singleton variant would be an unsound proof.
          set(state, dynamicBase, Initialized);
          recompute(state, dynamicBase);
        }
        break;
      }
      case IR::Opcode::Construct:
      case IR::Opcode::Repeat:
        if (instruction.Code != IR::Opcode::Repeat || instruction.Count != 0) {
          for (const auto value : instruction.Operands) {
            consume(state, value, report);
          }
        }
        if (resultPlace != IR::None) {
          set(state, resultPlace, Initialized);
          if (enumType(resultPlace)) {
            state[resultPlace].Variants = {instruction.Entity};
          }
        }
        break;
      case IR::Opcode::Call:
        for (const auto value : instruction.Operands) {
          consume(state, value, report);
        }
        if (resultPlace != IR::None) {
          set(state, resultPlace, Initialized);
        }
        break;
      default:
        break;
      }
    }
  }
  auto successors(Id block, const State &state,
                  bool report) const -> std::vector<std::pair<Id, State>> {
    const auto &end = Function.Blocks[block].End;
    CurrentOffset = end.SourceOffset;
    std::vector<std::pair<Id, State>> result;
    const auto edge = [&](const IR::Edge &destination, State outgoing,
                          bool implicit) {
      const auto &parameters = Function.Blocks[destination.Target].Parameters;
      if (!implicit) {
        for (const auto argument : destination.Arguments) {
          consume(outgoing, argument, report);
        }
        for (std::size_t i = 0; i < parameters.size(); ++i) {
          const auto target = Function.ValuePlaces[parameters[i]];
          if (target != IR::None) {
            set(outgoing, target, Initialized);
          }
        }
      }
      result.push_back({destination.Target, std::move(outgoing)});
    };
    if (end.Kind == IR::Exit::Jump) {
      edge(end.First, state, false);
    } else if (end.Kind == IR::Exit::Branch || end.Kind == IR::Exit::Checked ||
               end.Kind == IR::Exit::Bounds) {
      if (end.Kind == IR::Exit::Bounds) {
        locate(state, end.Place, report);
      }
      const bool implicit = end.Kind != IR::Exit::Branch;
      edge(end.First, state, implicit);
      edge(end.Second, state, implicit);
    } else if (end.Kind == IR::Exit::Variant) {
      locate(state, end.Place, report);
      require(state[end.Place].Tag == Initialized,
              "enum discriminant is moved or uninitialized", report);
      auto selected = state;
      if (selected[end.Place].Variants.contains(end.Variant)) {
        selected[end.Place].Variants = {end.Variant};
        edge(end.First, std::move(selected), false);
      }
      auto rejected = state;
      rejected[end.Place].Variants.erase(end.Variant);
      if (!rejected[end.Place].Variants.empty()) {
        edge(end.Second, std::move(rejected), false);
      }
    } else if (end.Kind == IR::Exit::Return) {
      auto outgoing = state;
      consume(outgoing, end.Operands[0], report);
      for (const auto &fact : outgoing) {
        require(fact.ProtectMax == 0,
                "function returns with an active index-base protection",
                report);
      }
    }
    return result;
  }
  auto merge(Id block, const State &state) -> bool {
    if (!Incoming[block]) {
      Incoming[block] = state;
      changes(state.size());
      return true;
    }
    auto &target = *Incoming[block];
    const auto possible = [&](const State &facts, Id id) {
      for (auto place = id; place != IR::None;
           place = Function.Places[place].Parent) {
        const auto &projection = Function.Places[place];
        if (projection.Form == IR::Projection::VariantField &&
            !facts[projection.Parent].Variants.contains(projection.Variant)) {
          return false;
        }
      }
      return true;
    };
    std::vector<bool> previouslyPossible(state.size());
    for (Id i = 0; i < state.size(); ++i) {
      previouslyPossible[i] = possible(target, i);
    }
    bool different = false;
    for (std::size_t i = 0; i < state.size(); ++i) {
      // Payload facts are conditional on their enclosing variant. A path on
      // which that variant is absent must not inject a moved/uninitialized
      // payload into an unrelated, constructible alternative.
      if (!possible(state, static_cast<Id>(i))) {
        continue;
      }
      if (!previouslyPossible[i]) {
        if (target[i] != state[i]) {
          target[i] = state[i];
          changes(1);
          different = true;
        }
        continue;
      }
      auto combined = target[i];
      combined.State |= state[i].State;
      combined.Default |= state[i].Default;
      combined.Tag |= state[i].Tag;
      combined.Variants.insert(state[i].Variants.begin(),
                               state[i].Variants.end());
      combined.ProtectMin = std::min(combined.ProtectMin, state[i].ProtectMin);
      combined.ProtectMax = std::max(combined.ProtectMax, state[i].ProtectMax);
      if (combined != target[i]) {
        target[i] = std::move(combined);
        changes(1);
        different = true;
      }
    }
    return different;
  }
  auto changes(std::size_t amount) -> void {
    if (amount >
        Boundaries.FactChanges - std::min(Changes, Boundaries.FactChanges)) {
      throw std::logic_error(
          "initialization analysis resource budget exhausted (fixed point)");
    }
    Changes += amount;
  }
  const IR::Draft &Program;
  const IR::Function &Function;
  Limits Boundaries;
  std::vector<std::vector<Id>> Children;
  std::vector<std::optional<State>> Incoming;
  std::size_t Changes = 0;
  mutable std::size_t CurrentOffset = 0;
};
} // namespace
auto Analyze(IR::Program program, Diagnostic::Diagnostics &diagnostics,
             Limits limits) -> std::optional<InitializationCheckedView> {
  try {
    for (const auto &function : program.storage().Functions) {
      Analysis(program.storage(), function, limits).run();
    }
    return InitializationCheckedView(std::move(program));
  } catch (const SourceFailure &failure) {
    diagnostics.sourceError(failure.Offset,
                            std::string("initialization analysis: ") +
                                failure.what());
    return std::nullopt;
  } catch (const std::logic_error &failure) {
    diagnostics.error("initialization analysis", failure.what());
    return std::nullopt;
  }
}
} // namespace Chtholly::Flow
