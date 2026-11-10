#pragma once

#include "WorldGen/Sampling/BiomeSampler.h"
#include <glm/glm.hpp>
#include <optional>
#include <string>

namespace cutum
{

struct WorldGenPack;

class UBiomeRegistry
{
public:
  void LoadFromPack(const WorldGenPack &pack);
  BiomeId IdFromString(const std::string &name) const;
  const char *StringFromId(BiomeId id) const;
  std::optional<glm::ivec3> MapColorFor(BiomeId id) const;
};

UBiomeRegistry &GetActiveBiomeRegistry();

BiomeId BiomeIdFromString(const std::string &name);
const char *BiomeIdToString(BiomeId biome);

} // namespace cutum
