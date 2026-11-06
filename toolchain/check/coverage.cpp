// SPDX-License-Identifier: MIT OR Apache-2.0
#include "checker.h"
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace Chtholly::Check {
using Id = SemIR::Id;
using Type = Types::TypeId;
auto Checker::coverage(Type input, const std::vector<Id> &patterns, Id node,
                       bool diagnose, Id candidate) -> bool {
  using Row = std::vector<Id>;
  enum class Outcome { Exhaustive, Missing, IntegerFallback, ResourceExceeded };
  struct CoverageResult {
    Outcome Kind;
    std::vector<std::string> Values;
  };
  std::size_t remaining = CoverageLimits.Candidates;
  const auto wildcard = [&](Id id) {
    return id == SemIR::None ||
           Result.Patterns[id].Form == SemIR::PatternKind::Wildcard ||
           Result.Patterns[id].Form == SemIR::PatternKind::Binding;
  };
  std::function<CoverageResult(std::vector<Type>, std::vector<Row>, Row,
                               unsigned)>
      solve;
  solve = [&](std::vector<Type> types, std::vector<Row> rows, Row constraint,
              unsigned depth) -> CoverageResult {
    if (remaining == 0 || depth > CoverageLimits.Depth) {
      return {Outcome::ResourceExceeded, {}};
    }
    --remaining;
    // The entire remaining product must be checked before choosing its first
    // column. A later ! field empties the domain just as an earlier one does.
    for (const auto type : types) {
      if (!TypeBuilder.inhabited(type)) {
        return {Outcome::Exhaustive, {}};
      }
    }
    if (types.empty()) {
      return rows.empty() ? CoverageResult{Outcome::Missing, {}}
                          : CoverageResult{Outcome::Exhaustive, {}};
    }
    if (constraint[0] != SemIR::None &&
        Result.Patterns[constraint[0]].Form == SemIR::PatternKind::Or) {
      for (const auto alternative : Result.Patterns[constraint[0]].Children) {
        auto selected = constraint;
        selected[0] = alternative;
        const auto result = solve(types, rows, std::move(selected), depth + 1);
        if (result.Kind != Outcome::Exhaustive) {
          return result;
        }
      }
      return {Outcome::Exhaustive, {}};
    }
    std::vector<Row> expanded;
    std::vector<Row> pending = std::move(rows);
    while (!pending.empty()) {
      auto row = std::move(pending.back());
      pending.pop_back();
      if (!row.empty() && row[0] != SemIR::None &&
          Result.Patterns[row[0]].Form == SemIR::PatternKind::Or) {
        for (const auto child : Result.Patterns[row[0]].Children) {
          auto alternative = row;
          alternative[0] = child;
          pending.push_back(std::move(alternative));
          if (pending.size() + expanded.size() > remaining) {
            return {Outcome::ResourceExceeded, {}};
          }
        }
      } else {
        expanded.push_back(std::move(row));
      }
    }
    const auto first = types[0];
    std::vector<Type> tail(types.begin() + 1, types.end());
    std::vector<std::vector<Types::Field>> constructors;
    std::vector<std::string> names;
    std::vector<Id> tags;
    if (first == Type::Bool) {
      constructors.resize(2);
      names = {"false", "true"};
      tags = {0, 1};
    } else if (first.owner() != nullptr) {
      const auto &type = TypeBuilder.get(first);
      if (type.Form == Types::Kind::Enum) {
        for (Id i = 0; i < type.Variants.size(); ++i) {
          if (type.Variants[i].Inhabited) {
            constructors.push_back(type.Variants[i].Fields);
            names.push_back(type.Name + "::" + type.Variants[i].Name);
            tags.push_back(i);
          }
        }
      } else if (type.Form == Types::Kind::Struct ||
                 type.Form == Types::Kind::Tuple) {
        constructors.push_back(type.Fields);
        names.push_back(type.Name);
        tags.push_back(SemIR::None);
      }
    } else if (first == Type::Unit) {
      constructors.resize(1);
      names = {"()"};
      tags = {SemIR::None};
    }
    if (constructors.empty()) {
      std::vector<Row> defaults;
      const auto exact =
          constraint[0] != SemIR::None &&
          Result.Patterns[constraint[0]].Form == SemIR::PatternKind::Integer;
      for (const auto &row : expanded) {
        bool matches = wildcard(row[0]);
        if (!matches && exact &&
            Result.Patterns[row[0]].Form == SemIR::PatternKind::Integer) {
          matches = Result.Patterns[row[0]].Literal->bits() ==
                    Result.Patterns[constraint[0]].Literal->bits();
        }
        if (matches) {
          defaults.emplace_back(row.begin() + 1, row.end());
        }
      }
      if (Types::IsInteger(first) && defaults.empty() && !exact) {
        return {diagnose ? Outcome::IntegerFallback : Outcome::Missing, {}};
      }
      auto result =
          solve(tail, std::move(defaults),
                Row(constraint.begin() + 1, constraint.end()), depth + 1);
      if (diagnose && Types::IsInteger(first) && !exact &&
          result.Kind == Outcome::Missing) {
        return {Outcome::IntegerFallback, {}};
      }
      if (diagnose && result.Kind == Outcome::Missing) {
        std::string value;
        if (first == Type::F32 || first == Type::F64) {
          value = "0.0" + describe(first);
        } else if (first.owner() != nullptr &&
                   TypeBuilder.get(first).Form == Types::Kind::Array) {
          const auto &array = TypeBuilder.get(first);
          value = array.Length == 0
                      ? "[]"
                      : "<" + describe(first) +
                            ": any independently constructed elements>";
        } else {
          value = "<" + describe(first) + ": any valid value>";
        }
        result.Values.insert(result.Values.begin(), std::move(value));
      }
      return result;
    }
    for (std::size_t c = 0; c < constructors.size(); ++c) {
      const auto &fields = constructors[c];
      std::vector<Type> nested;
      for (const auto &field : fields) {
        nested.push_back(field.Type);
      }
      nested.insert(nested.end(), tail.begin(), tail.end());
      Row narrowed(fields.size(), SemIR::None);
      if (!wildcard(constraint[0])) {
        const auto &wanted = Result.Patterns[constraint[0]];
        bool matches = false;
        if (first == Type::Bool) {
          matches = wanted.Form == SemIR::PatternKind::Boolean &&
                    wanted.Literal &&
                    wanted.Literal->boolean() == (tags[c] != 0);
        } else if (wanted.Form == SemIR::PatternKind::Variant) {
          matches = wanted.Variant == tags[c];
        } else {
          matches = wanted.Form == SemIR::PatternKind::Product ||
                    wanted.Form == SemIR::PatternKind::Unit;
        }
        if (!matches) {
          continue;
        }
        for (std::size_t i = 0; i < wanted.Children.size(); ++i) {
          narrowed.at(wanted.Fields[i]) = wanted.Children[i];
        }
      }
      narrowed.insert(narrowed.end(), constraint.begin() + 1, constraint.end());
      std::vector<Row> selected;
      for (const auto &row : expanded) {
        Row prefix(fields.size(), SemIR::None);
        if (!wildcard(row[0])) {
          const auto &pattern = Result.Patterns[row[0]];
          bool matches = false;
          if (first == Type::Bool) {
            matches = pattern.Form == SemIR::PatternKind::Boolean &&
                      pattern.Literal &&
                      pattern.Literal->boolean() == (tags[c] != 0);
          } else if (pattern.Form == SemIR::PatternKind::Variant) {
            matches = pattern.Variant == tags[c];
          } else {
            matches = pattern.Form == SemIR::PatternKind::Product ||
                      pattern.Form == SemIR::PatternKind::Unit;
          }
          if (!matches) {
            continue;
          }
          for (std::size_t i = 0; i < pattern.Children.size(); ++i) {
            prefix.at(pattern.Fields[i]) = pattern.Children[i];
          }
        }
        prefix.insert(prefix.end(), row.begin() + 1, row.end());
        selected.push_back(std::move(prefix));
      }
      auto result = solve(std::move(nested), std::move(selected),
                          std::move(narrowed), depth + 1);
      if (result.Kind != Outcome::Exhaustive) {
        if (diagnose && result.Kind == Outcome::Missing) {
          if (result.Values.size() < fields.size()) {
            throw std::logic_error("coverage witness lost a product component");
          }
          std::string value = names[c];
          if (first.owner() != nullptr) {
            const bool tuple =
                TypeBuilder.get(first).Form == Types::Kind::Tuple;
            value = tuple ? "(" : names[c] + " { ";
            for (std::size_t field = 0; field < fields.size(); ++field) {
              if (field != 0) {
                value += ", ";
              }
              if (!tuple) {
                value += fields[field].Name + ": ";
              }
              value += result.Values[field];
            }
            if (tuple && fields.size() == 1) {
              value += ',';
            }
            value += tuple ? ")" : " }";
          }
          result.Values.erase(result.Values.begin(),
                              result.Values.begin() +
                                  static_cast<std::ptrdiff_t>(fields.size()));
          result.Values.insert(result.Values.begin(), std::move(value));
        }
        return result;
      }
    }
    return {Outcome::Exhaustive, {}};
  };
  std::vector<Row> rows;
  for (const auto pattern : patterns) {
    rows.push_back({pattern});
  }
  const auto result = solve({input}, std::move(rows), {candidate}, 0);
  if (!diagnose && result.Kind != Outcome::ResourceExceeded) {
    return result.Kind == Outcome::Exhaustive;
  }
  switch (result.Kind) {
  case Outcome::Exhaustive:
    return true;
  case Outcome::Missing:
    if (result.Values.size() != 1) {
      throw std::logic_error("coverage did not produce one input witness");
    }
    fail(node, "non-exhaustive pattern coverage; constructible shape: " +
                   result.Values[0]);
    break;
  case Outcome::IntegerFallback:
    fail(node, "integer patterns require a wildcard or binding fallback");
    break;
  case Outcome::ResourceExceeded:
    fail(node, "pattern coverage resource budget exhausted; no missing-case "
               "proof was produced");
    break;
  }
  return false;
}
} // namespace Chtholly::Check
