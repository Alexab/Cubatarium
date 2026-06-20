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
  /// biome id -> prefab name -> weight multiplier
  std::unordered_map<std::string, std::unordered_map<std::string, float>>
      BiomeFeatureWeights;
};

class UWorldGenPack
{
public:
  static bool LoadFromDirectory(const std::string &packDir);
  static bool LoadPackId(const std::string &packId);
  static const WorldGenPack &Get();
  static const BiomeHeightProfile *HeightProfileFor(const std::string &biomeId);
  static float FeatureWeightMultiplier(const std::string &biomeId,
                                       const std::string &prefabName);
  static BiomeId BiomeAtImage(int worldX, int worldZ);

private:
  static WorldGenPack ActivePack;
};

} // namespace cutum
