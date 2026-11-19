#pragma once

#include "World/Lighting/SectionLightVolume.h"
#include "World/Math/WorldVerticalProfile.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace cutum
{
enum class SectionLightRestoreFailure
{
  None,
  Catalog,
  Profile,
  Configuration,
  Materials,
  Footprint,
  Checksum,
  UnknownInput,
  ContentMismatch,
  ReadSet,
  Install,
  Count
};
inline const char *
SectionLightRestoreFailureName(SectionLightRestoreFailure reason)
{
  switch (reason)
  {
  case SectionLightRestoreFailure::None:
    return "none";
  case SectionLightRestoreFailure::Catalog:
    return "catalog";
  case SectionLightRestoreFailure::Profile:
    return "profile";
  case SectionLightRestoreFailure::Configuration:
    return "configuration";
  case SectionLightRestoreFailure::Materials:
    return "materials";
  case SectionLightRestoreFailure::Footprint:
    return "footprint";
  case SectionLightRestoreFailure::Checksum:
    return "checksum";
  case SectionLightRestoreFailure::UnknownInput:
    return "unknown_input";
  case SectionLightRestoreFailure::ContentMismatch:
    return "content_mismatch";
  case SectionLightRestoreFailure::ReadSet:
    return "read_set";
  case SectionLightRestoreFailure::Install:
    return "install";
  default:
    return "invalid";
  }
}
struct SectionLightRestoreDiagnostic
{
  SectionLightRestoreFailure reason{SectionLightRestoreFailure::None};
  bool has_input{false}, current_known{false};
  glm::ivec3 input_coord{0};
  uint64_t expected{0}, current{0};
};
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
