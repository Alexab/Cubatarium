#pragma once

#include "WorldGen/Sampling/BiomeSampler.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cutum
{

enum class PrefabFeaturePool
{
  Vegetation,
  Decoration,
  Structures
};

struct PrefabFeatureRule
{
  std::string PrefabName;
  std::vector<BiomeId> Biomes;
  int Spacing{0};
  int Weight{1};
  int ChancePerColumn{0};
  uint32_t SeedOffset{0};
  int PlacementYOffset{0};
};

struct PrefabFeatureConfig
{
  std::vector<PrefabFeatureRule> Vegetation;
  std::vector<PrefabFeatureRule> Decoration;
  std::vector<PrefabFeatureRule> Structures;
};

class UPrefabFeatureConfigStorage
{
public:
  static bool LoadFromFile(const std::filesystem::path &path);
  static const PrefabFeatureConfig &Get();
  static bool IsLoaded();

private:
  static PrefabFeatureConfig Config;
  static bool Loaded;
};

BiomeId BiomeIdFromString(const std::string &name);
const char *BiomeIdToString(BiomeId biome);

} // namespace cutum
