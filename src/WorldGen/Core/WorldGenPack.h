#pragma once

#include "WorldGen/Sampling/BiomeSampler.h"
#include <string>
#include <unordered_map>

namespace cutum
{

enum class WorldGenBiomeMode
{
  Procedural,
  Image
};

struct WorldGenPack
{
  std::string Id{"default"};
  WorldGenBiomeMode BiomeMode{WorldGenBiomeMode::Procedural};
  std::string BiomeMapImagePath;
  int BiomeMapBlockScale{4};
  std::unordered_map<std::string, BiomeHeightProfile> BiomeHeightProfiles;
};

class UWorldGenPack
{
public:
  static bool LoadFromDirectory(const std::string &packDir);
  static const WorldGenPack &Get();
  static const BiomeHeightProfile *HeightProfileFor(const std::string &biomeId);
  static BiomeId BiomeAtImage(int worldX, int worldZ);

private:
  static WorldGenPack ActivePack;
};

} // namespace cutum
