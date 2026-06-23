#pragma once

#include "WorldGen/Features/PrefabFeatureConfig.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

enum class WorldGenBiomeMode
{
  Procedural,
  Image
};

struct BiomeSubBiomePackRule
{
  std::string SubsurfaceSlot;
  float VegetationWeightMul{1.0f};
  float DecorationWeightMul{1.0f};
  float NoiseThreshold{-1.0f};
};

struct BiomePackDefinition
{
  BiomeHeightProfile Height;
  std::string SurfaceSlot;
  std::string SubsurfaceSlot;
  std::unordered_map<std::string, float> FeatureWeights;
  std::unordered_map<std::string, BiomeSubBiomePackRule> SubBiomes;
};

struct WorldGenPackPipeline
{
  bool Loaded{false};
  bool Fluids{false};
  bool Ravines{false};
  bool Ores{false};
  bool Caves{false};
  bool Vegetation{false};
  bool GroundCover{false};
  bool Decoration{false};
  bool Structures{false};
  bool LavaPools{false};
  bool FirePatch{false};
};

struct WorldGenPack
{
  std::string Id{"default"};
  WorldGenBiomeMode BiomeMode{WorldGenBiomeMode::Procedural};
  std::string BiomeMapImagePath;
  int BiomeMapBlockScale{4};
  float BiomeBlendRadius{-1.0f};
  WorldGenPackPipeline Pipeline;
  std::unordered_map<std::string, BiomePackDefinition> Biomes;
};

class UWorldGenPack
{
public:
  static bool LoadFromDirectory(const std::string &packDir);
  static bool LoadPackId(const std::string &packId);
  static bool ReloadActive();
  static std::vector<std::string> ListPackIds();
  static const WorldGenPack &Get();
  static const BiomeHeightProfile *HeightProfileFor(const std::string &biomeId);
  static const BiomePackDefinition *BiomeDefinitionFor(const std::string &biomeId);
  static float FeatureWeightMultiplier(const std::string &biomeId,
                                       const std::string &prefabName);
  static float SubBiomePoolWeightMultiplier(const std::string &biomeId,
                                            SubBiomeId subBiome,
                                            PrefabFeaturePool pool);
  static BiomeId BiomeAtImage(int worldX, int worldZ);

private:
  static WorldGenPack ActivePack;
  static std::string ActivePackDir;
};

} // namespace cutum
