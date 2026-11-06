// SPDX-License-Identifier: MIT OR Apache-2.0
#include <Chtholly/Target/layout.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Chtholly::Target {
struct Layouts::Storage {
  Types::Table Types;
  Description Target;
  std::vector<ObjectLayout> Objects;
};
namespace {
constexpr auto Maximum =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
auto Add(std::uint64_t left, std::uint64_t right) -> std::uint64_t {
  if (right > Maximum || left > Maximum - right) {
    throw std::logic_error("target object exceeds isize::MAX");
  }
  return left + right;
}
auto Align(std::uint64_t value, std::uint64_t alignment) -> std::uint64_t {
  const auto remainder = value % alignment;
  return remainder == 0 ? value : Add(value, alignment - remainder);
}
auto PrimitiveLayout(Types::TypeId type, Description target) -> ObjectLayout {
  if (type == Types::TypeId::Unit || type == Types::TypeId::Never) {
    return {};
  }
  const auto scalar = ScalarOf(type);
  const auto alignment = target.alignment(scalar);
  if (alignment == 0) {
    throw std::logic_error("invalid primitive layout");
  }
  const auto bytes = (target.bits(scalar) + 7U) / 8U;
  return {bytes, alignment, bytes, {}, 0, 0, {}};
}
} // namespace
auto EnumTagBits(std::uint64_t count) -> unsigned {
  if (count == 0) {
    return 0;
  }
  if (count <= 256) {
    return 8;
  }
  if (count <= 65536) {
    return 16;
  }
  return count <= (std::uint64_t{1} << 32) ? 32U : 64U;
}
auto Layouts::Build(Types::Table types, std::string &error,
                    Description target) -> std::optional<Layouts> {
  try {
    if (!target.valid()) {
      throw std::logic_error("unsupported target layout");
    }
    auto data =
        std::make_shared<Storage>(Storage{std::move(types), WindowsX64, {}});
    data->Objects.resize(data->Types.records().size());
    std::vector<unsigned char> state(data->Objects.size(), 0);
    const auto layout = [&](Types::TypeId type) {
      if (type.owner() == nullptr) {
        return PrimitiveLayout(type, target);
      }
      const auto index =
          type.index() - static_cast<unsigned>(Types::Primitive::Never) - 1;
      if (!data->Types.contains(type) || state.at(index) != 2) {
        throw std::logic_error("layout requested before dependency completion");
      }
      return data->Objects[index];
    };
    for (std::uint32_t root = 0; root < data->Objects.size(); ++root) {
      std::vector<std::pair<std::uint32_t, bool>> work{{root, false}};
      while (!work.empty()) {
        const auto [index, finish] = work.back();
        work.pop_back();
        if (state[index] == 2) {
          continue;
        }
        const auto &record = data->Types.records()[index];
        if (!finish) {
          if (state[index] == 1) {
            throw std::logic_error("cyclic completed type graph");
          }
          state[index] = 1;
          work.push_back({index, true});
          const auto dependency = [&](Types::TypeId type) {
            if (type.owner() != nullptr) {
              if (!data->Types.contains(type)) {
                throw std::logic_error("foreign layout type");
              }
              const auto firstAggregate =
                  static_cast<unsigned>(Types::Primitive::Never) + 1;
              const auto child = type.index() - firstAggregate;
              if (state[child] != 2) {
                work.push_back({child, false});
              }
            }
          };
          if (record.Form == Types::Kind::Array) {
            dependency(record.Element);
          }
          for (const auto &field : record.Fields) {
            dependency(field.Type);
          }
          for (const auto &variant : record.Variants) {
            for (const auto &field : variant.Fields) {
              dependency(field.Type);
            }
          }
          continue;
        }
        const auto product = [&](const std::vector<Types::Field> &fields) {
          ObjectLayout result;
          for (const auto &field : fields) {
            const auto child = layout(field.Type);
            result.Alignment = std::max(result.Alignment, child.Alignment);
            const auto offset = Align(result.Size, child.Alignment);
            result.Fields.push_back(offset);
            result.Size = Add(offset, child.Size);
          }
          result.Size = Align(result.Size, result.Alignment);
          result.Stride = result.Size;
          return result;
        };
        ObjectLayout result;
        if (record.Form == Types::Kind::Array) {
          const auto element = layout(record.Element);
          if (element.Stride != 0 && record.Length > Maximum / element.Stride) {
            throw std::logic_error("target array exceeds isize::MAX");
          }
          result.Size = element.Stride * record.Length;
          result.Stride = result.Size;
          result.Alignment = element.Alignment;
        } else if (record.Form != Types::Kind::Enum) {
          result = product(record.Fields);
        } else if (!record.Variants.empty()) {
          const auto count = static_cast<std::uint64_t>(record.Variants.size());
          result.TagBits = EnumTagBits(count);
          result.Alignment = result.TagBits / 8U;
          std::uint64_t payloadSize = 0;
          std::uint64_t payloadAlign = 1;
          for (const auto &variant : record.Variants) {
            auto payload = product(variant.Fields);
            payloadSize = std::max(payloadSize, payload.Size);
            payloadAlign = std::max(payloadAlign, payload.Alignment);
            result.Variants.push_back(std::move(payload));
          }
          result.Alignment = std::max(result.Alignment, payloadAlign);
          result.PayloadOffset = Align(result.TagBits / 8U, payloadAlign);
          result.Size =
              Align(Add(result.PayloadOffset, payloadSize), result.Alignment);
          result.Stride = result.Size;
        }
        data->Objects[index] = std::move(result);
        state[index] = 2;
      }
    }
    return Layouts(std::move(data));
  } catch (const std::logic_error &failure) {
    error = failure.what();
    return std::nullopt;
  }
}
auto Layouts::get(Types::TypeId type) const -> ObjectLayout {
  if (!Data->Types.contains(type)) {
    throw std::logic_error("layout type has a foreign identity");
  }
  if (type.owner() == nullptr) {
    return PrimitiveLayout(type, Data->Target);
  }
  return Data->Objects.at(type.index() -
                          static_cast<unsigned>(Types::Primitive::Never) - 1);
}
auto Layouts::types() const -> const Types::Table & { return Data->Types; }
auto Layouts::target() const -> Description { return Data->Target; }
auto Layouts::indirect(Types::TypeId type) const -> bool {
  return Data->Types.inhabited(type) && type.owner() != nullptr &&
         get(type).Size != 0;
}
} // namespace Chtholly::Target
