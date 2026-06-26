#pragma once

#include "WorldGen/Sampling/BiomeSampler.h"
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <unordered_map>

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

private:
  std::unordered_map<std::string, BiomeId> StringToId;
  std::unordered_map<BiomeId, std::string> IdToString;
  std::unordered_map<BiomeId, glm::ivec3> MapColors;
};

UBiomeRegistry &GetActiveBiomeRegistry();

BiomeId BiomeIdFromString(const std::string &name);
const char *BiomeIdToString(BiomeId biome);

} // namespace cutum
