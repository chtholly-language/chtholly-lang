// SPDX-License-Identifier: MIT OR Apache-2.0
#pragma once
#include <Chtholly/Target/target.h>
#include <memory>
#include <string>
#include <vector>

namespace Chtholly::Target {
auto EnumTagBits(std::uint64_t variantCount) -> unsigned;
struct ObjectLayout {
  std::uint64_t Size = 0;
  std::uint64_t Alignment = 1;
  std::uint64_t Stride = 0;
  std::vector<std::uint64_t> Fields;
  unsigned TagBits = 0;
  std::uint64_t PayloadOffset = 0;
  std::vector<ObjectLayout> Variants;
};
class Layouts {
public:
  static auto Build(Types::Table types, std::string &error,
                    Description target = WindowsX64) -> std::optional<Layouts>;
  auto get(Types::TypeId type) const -> ObjectLayout;
  auto types() const -> const Types::Table &;
  auto target() const -> Description;
  auto indirect(Types::TypeId type) const -> bool;

private:
  struct Storage;
  explicit Layouts(std::shared_ptr<const Storage> data)
      : Data(std::move(data)) {}
  std::shared_ptr<const Storage> Data;
};
} // namespace Chtholly::Target
