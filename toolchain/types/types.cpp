// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Types/types.h>

#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace Chtholly::Types {
struct Table::Storage {
  std::vector<Record> Records;
  std::vector<bool> Defined;
  std::vector<bool> CopyRequested;
};
namespace {
constexpr std::array<std::string_view, 18> Names = {
    "<invalid>", "i8",   "i16",   "i32",   "i64", "i128", "u8",   "u16", "u32",
    "u64",       "u128", "isize", "usize", "f32", "f64",  "bool", "()",  "!"};
constexpr auto FirstAggregate = static_cast<unsigned>(Primitive::Never) + 1;
auto Dependencies(const Record &record) -> std::vector<TypeId> {
  std::vector<TypeId> result;
  for (const auto &field : record.Fields) {
    result.push_back(field.Type);
  }
  for (const auto &variant : record.Variants) {
    for (const auto &field : variant.Fields) {
      result.push_back(field.Type);
    }
  }
  if (record.Form == Kind::Array) {
    result.push_back(record.Element);
  }
  return result;
}
} // namespace
auto Name(Primitive type) -> std::string_view {
  const auto index = static_cast<unsigned>(type);
  return index < Names.size() ? Names[index] : Names[0];
}
auto Lookup(std::string_view name) -> std::optional<TypeId> {
  for (unsigned i = 1; i < Names.size(); ++i) {
    if (Names[i] == name) {
      return TypeId(static_cast<Primitive>(i));
    }
  }
  return std::nullopt;
}
auto IsInteger(Primitive type) -> bool {
  return type >= Primitive::I8 && type <= Primitive::USize;
}
auto IsSigned(Primitive type) -> bool {
  return (type >= Primitive::I8 && type <= Primitive::I128) ||
         type == Primitive::ISize;
}
auto IsFloat(Primitive type) -> bool {
  return type == Primitive::F32 || type == Primitive::F64;
}
auto IsNumeric(Primitive type) -> bool {
  return IsInteger(type) || IsFloat(type);
}
auto Table::contains(TypeId type) const -> bool {
  if (type == TypeId::Invalid) {
    return false;
  }
  return type.owner() == nullptr
             ? type.index() < FirstAggregate
             : type.owner() == Data.get() && type.index() >= FirstAggregate &&
                   type.index() - FirstAggregate < Data->Records.size();
}
auto Table::get(TypeId type) const -> const Record & {
  if (!contains(type) || type.owner() == nullptr) {
    throw std::logic_error("aggregate type does not belong to this table");
  }
  return Data->Records.at(type.index() - FirstAggregate);
}
auto Table::inhabited(TypeId type) const -> bool {
  if (!contains(type)) {
    throw std::logic_error("invalid type identity");
  }
  return type.owner() == nullptr ? type != TypeId::Never : get(type).Inhabited;
}
auto Table::copy(TypeId type) const -> bool {
  if (!contains(type)) {
    throw std::logic_error("invalid type identity");
  }
  return type.owner() == nullptr ? type != TypeId::Never : get(type).Copy;
}
auto Table::describe(TypeId type) const -> std::string {
  if (type.owner() == nullptr) {
    return std::string(Name(type.primitive()));
  }
  return get(type).Name;
}
auto Table::records() const -> std::span<const Record> { return Data->Records; }
auto Table::id(std::uint32_t index) const -> TypeId {
  if (index >= Data->Records.size()) {
    throw std::out_of_range("type record index");
  }
  return TypeId(index + FirstAggregate, Data.get());
}
Builder::Builder() : Data(std::make_shared<Table::Storage>()) {}
auto Builder::declare(Kind kind, std::string name) -> TypeId {
  if (kind != Kind::Struct && kind != Kind::Enum) {
    throw std::logic_error(
        "structural types must use tuple or array interning");
  }
  return createRecord(kind, std::move(name));
}
auto Builder::createRecord(Kind kind, std::string name) -> TypeId {
  if (Frozen ||
      Data->Records.size() >=
          std::numeric_limits<std::uint32_t>::max() - FirstAggregate) {
    throw std::logic_error(
        "type builder is frozen or its identity space is exhausted");
  }
  const auto index = static_cast<std::uint32_t>(Data->Records.size());
  Record record;
  record.Form = kind;
  record.Name = std::move(name);
  Data->Records.push_back(std::move(record));
  Data->Defined.push_back(false);
  Data->CopyRequested.push_back(false);
  return TypeId(index + FirstAggregate, Data.get());
}
auto Builder::get(TypeId type) const -> const Record & {
  return Table(Data).get(type);
}
auto Builder::define(TypeId type, std::vector<Field> fields,
                     std::vector<Variant> variants) -> void {
  if (Frozen || type.owner() != Data.get() || type.index() < FirstAggregate) {
    throw std::logic_error("cannot define a foreign or frozen type");
  }
  const auto index = type.index() - FirstAggregate;
  if (Data->Defined.at(index)) {
    throw std::logic_error("type defined twice");
  }
  const auto kind = Data->Records[index].Form;
  if ((kind != Kind::Struct && kind != Kind::Enum) ||
      (kind == Kind::Struct && !variants.empty()) ||
      (kind == Kind::Enum && !fields.empty())) {
    throw std::logic_error("definition shape disagrees with its nominal type");
  }
  Data->Records[index].Fields = std::move(fields);
  Data->Records[index].Variants = std::move(variants);
  Data->Defined[index] = true;
}
auto Builder::tuple(std::vector<TypeId> elements) -> TypeId {
  if (elements.empty()) {
    return TypeId::Unit;
  }
  for (std::uint32_t i = 0; i < Data->Records.size(); ++i) {
    const auto &record = Data->Records[i];
    if (record.Form != Kind::Tuple || record.Fields.size() != elements.size()) {
      continue;
    }
    bool equal = true;
    for (std::size_t j = 0; j < elements.size(); ++j) {
      equal &= record.Fields[j].Type == elements[j];
    }
    if (equal) {
      return TypeId(i + FirstAggregate, Data.get());
    }
  }
  std::string spelling = "(";
  const Table spellingTable(Data);
  for (std::size_t i = 0; i < elements.size(); ++i) {
    if (i != 0) {
      spelling += ", ";
    }
    spelling += spellingTable.describe(elements[i]);
  }
  if (elements.size() == 1) {
    spelling += ',';
  }
  spelling += ')';
  const auto type = createRecord(Kind::Tuple, std::move(spelling));
  std::vector<Field> fields;
  for (std::size_t i = 0; i < elements.size(); ++i) {
    fields.push_back({std::to_string(i), elements[i]});
  }
  auto &record = Data->Records[type.index() - FirstAggregate];
  record.Fields = std::move(fields);
  Data->Defined[type.index() - FirstAggregate] = true;
  record.Inhabited = true;
  record.Copy = true;
  const Table table(Data);
  for (const auto element : elements) {
    record.Inhabited &= table.contains(element) && table.inhabited(element);
    record.Copy &= table.contains(element) && table.copy(element);
  }
  return type;
}
auto Builder::array(TypeId element, std::uint64_t length) -> TypeId {
  for (std::uint32_t i = 0; i < Data->Records.size(); ++i) {
    const auto &record = Data->Records[i];
    if (record.Form == Kind::Array && record.Element == element &&
        record.Length == length) {
      return TypeId(i + FirstAggregate, Data.get());
    }
  }
  const auto type =
      createRecord(Kind::Array, Table(Data).describe(element) + "[" +
                                    std::to_string(length) + "]");
  auto &record = Data->Records[type.index() - FirstAggregate];
  record.Element = element;
  record.Length = length;
  const Table table(Data);
  record.Inhabited =
      length == 0 || (table.contains(element) && table.inhabited(element));
  record.Copy = length == 0 || (table.contains(element) && table.copy(element));
  Data->Defined[type.index() - FirstAggregate] = true;
  return type;
}
auto Builder::requestCopy(TypeId type) -> bool {
  if (Frozen || type.owner() != Data.get() || type.index() < FirstAggregate) {
    return false;
  }
  const auto index = type.index() - FirstAggregate;
  if (index >= Data->Records.size() || Data->CopyRequested[index] ||
      (Data->Records[index].Form != Kind::Struct &&
       Data->Records[index].Form != Kind::Enum)) {
    return false;
  }
  Data->CopyRequested[index] = true;
  return true;
}
auto Builder::inhabited(TypeId type) const -> bool {
  return Table(Data).inhabited(type);
}
auto Builder::copy(TypeId type) const -> bool { return Table(Data).copy(type); }
auto Builder::complete(std::vector<Error> &errors) -> bool {
  if (Frozen) {
    throw std::logic_error("type table already completed");
  }
  const Table table(Data);
  const auto count = Data->Records.size();
  std::vector<std::vector<std::uint32_t>> users(count);
  std::vector<std::size_t> pending(count, 0);
  std::deque<std::uint32_t> ready;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto type = TypeId(i + FirstAggregate, Data.get());
    const auto &record = Data->Records[i];
    const auto validateFields = [&](const std::vector<Field> &fields) {
      std::unordered_set<std::string> names;
      for (const auto &field : fields) {
        if (field.Name.empty() || field.Name == "_" ||
            !names.insert(field.Name).second) {
          errors.push_back({ErrorKind::InvalidDefinition, type,
                            "duplicate or invalid field identity"});
        }
      }
    };
    validateFields(record.Fields);
    std::unordered_set<std::string> variants;
    for (const auto &variant : record.Variants) {
      if (variant.Name.empty() || variant.Name == "_" ||
          !variants.insert(variant.Name).second) {
        errors.push_back({ErrorKind::InvalidDefinition, type,
                          "duplicate or invalid variant identity"});
      }
      validateFields(variant.Fields);
    }
    if (!Data->Defined[i]) {
      errors.push_back(
          {ErrorKind::Incomplete, type, "type declaration was not completed"});
    }
    for (const auto dependency : Dependencies(Data->Records[i])) {
      if (!table.contains(dependency)) {
        errors.push_back({ErrorKind::ForeignIdentity, type,
                          "field has an invalid or foreign type"});
      } else if (dependency.owner() != nullptr) {
        ++pending[i];
        users[dependency.index() - FirstAggregate].push_back(i);
      }
    }
    if (pending[i] == 0) {
      ready.push_back(i);
    }
  }
  std::size_t completed = 0;
  while (!ready.empty()) {
    const auto index = ready.front();
    ready.pop_front();
    ++completed;
    auto &record = Data->Records[index];
    record.Inhabited = false;
    record.Copy = false;
    const auto all = [&](const std::vector<Field> &fields, bool copy) {
      return std::all_of(fields.begin(), fields.end(), [&](const Field &field) {
        return table.contains(field.Type) &&
               (copy ? table.copy(field.Type) : table.inhabited(field.Type));
      });
    };
    bool eligible = true;
    if (record.Form == Kind::Array) {
      record.Inhabited =
          record.Length == 0 ||
          (table.contains(record.Element) && table.inhabited(record.Element));
      record.Copy = record.Length == 0 || (table.contains(record.Element) &&
                                           table.copy(record.Element));
    } else if (record.Form == Kind::Enum) {
      for (auto &variant : record.Variants) {
        variant.Inhabited = all(variant.Fields, false);
        record.Inhabited |= variant.Inhabited;
        eligible &= all(variant.Fields, true);
      }
      record.Copy = Data->CopyRequested[index] && eligible;
    } else {
      record.Inhabited = all(record.Fields, false);
      eligible = all(record.Fields, true);
      record.Copy =
          (record.Form == Kind::Tuple || Data->CopyRequested[index]) &&
          eligible;
    }
    if (Data->CopyRequested[index] && !eligible) {
      errors.push_back({ErrorKind::InvalidCopy,
                        TypeId(index + FirstAggregate, Data.get()),
                        "Copy requires every logical field to be Copy"});
    }
    for (const auto user : users[index]) {
      if (--pending[user] == 0) {
        ready.push_back(user);
      }
    }
  }
  if (completed != count) {
    for (std::uint32_t i = 0; i < count; ++i) {
      if (pending[i] != 0) {
        errors.push_back(
            {ErrorKind::ByValueCycle, TypeId(i + FirstAggregate, Data.get()),
             "by-value type dependency cycle (including zero-length arrays)"});
      }
    }
  }
  return errors.empty();
}
auto Builder::finish(std::vector<Error> &errors) -> std::optional<Table> {
  if (!complete(errors)) {
    return std::nullopt;
  }
  Frozen = true;
  return Table(Data);
}
} // namespace Chtholly::Types
