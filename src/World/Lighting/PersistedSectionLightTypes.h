#pragma once

#include "World/Lighting/SectionLightVolume.h"
#include "World/Math/WorldVerticalProfile.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace cutum
{
// Disk receipts describe content, never runtime incarnation/revision numbers.
struct PersistedSectionLight
{
  static constexpr size_t MaxDependencies = 65536;
  struct Input
  {
    glm::ivec3 coord{0};
    uint64_t content{0};
  };
  glm::ivec3 coord{0};
  WorldVerticalProfile profile;
  uint64_t configuration{0};
  std::string signature;
  std::vector<Input> inputs;
  SectionLightVolume volume;
  uint64_t checksum{0};
};
using PersistedColumnLight =
    std::vector<std::shared_ptr<const PersistedSectionLight>>;
inline constexpr size_t kMaxColumnLightSections = 256;
inline constexpr size_t kMaxColumnLightBytes = 8 * 1024 * 1024;

inline size_t PersistedLightBytes(const PersistedSectionLight &receipt)
{
  return sizeof(receipt) +
         receipt.inputs.capacity() * sizeof(receipt.inputs[0]) +
         receipt.signature.capacity() + 256;
}
} // namespace cutum
