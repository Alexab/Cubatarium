#pragma once

#include "WorldGen/Core/ProceduralSettings.h"
#include <cstdint>
#include <string>

namespace cutum
{

struct WorldSeedResolution
{
  std::string raw;
  uint32_t resolved{0};
  WorldSeedKind kind{WorldSeedKind::Numeric};
  WorldSeedHashAlgo hashAlgo{WorldSeedHashAlgo::Fnv1a32};
};

uint32_t Fnv1a32Seed(const std::string &text);
int32_t JavaStringHashSeed(const std::string &text);
bool IsNumericSeedText(const std::string &text);
WorldSeedResolution ResolveWorldSeed(const std::string &input,
                                     WorldSeedHashAlgo hashAlgo =
                                         WorldSeedHashAlgo::Fnv1a32);
const char *WorldSeedKindToString(WorldSeedKind kind);
WorldSeedKind WorldSeedKindFromString(const std::string &s);

} // namespace cutum
