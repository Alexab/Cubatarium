#pragma once

#include "WorldGen/Features/ObjectFeatureConfig.h"
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

struct WorldGenObjectEntry
{
  std::string ObjectName;
  std::vector<std::string> Biomes;
  int Spacing{0};
  int Weight{1};
  int ChancePerColumn{0};
  uint32_t SeedOffset{0};
  int PlacementYOffset{0};
  std::vector<std::string> SubBiomes;
  ObjectPlacementMode Mode{ObjectPlacementMode::Object};
  ScatterBlockSpec Scatter;
  SurfaceConstraint Surface;
};

struct WorldGenObjectSection
{
  std::vector<WorldGenObjectEntry> Entries;
};

struct WorldGenTerrainSlot
{
  std::string Block;
};

struct WorldGenOreSlot
{
  bool Enabled{true};
  float DensityMultiplier{1.0f};
};

struct WorldGenSets
{
  int SchemaVersion{1};
  std::unordered_map<std::string, WorldGenObjectSection> VegetationSections;
  std::unordered_map<std::string, WorldGenObjectSection> GroundCoverSections;
  std::unordered_map<std::string, WorldGenObjectSection> DecorationSections;
  std::unordered_map<std::string, WorldGenObjectSection> StructureSections;
  std::unordered_map<std::string, WorldGenTerrainSlot> Terrain;
  std::unordered_map<std::string, WorldGenOreSlot> Ores;
};

bool ParseWorldGenSets(const nlohmann::json &root, WorldGenSets &out,
                       std::string &error);
void WriteWorldGenSets(nlohmann::json &root, const WorldGenSets &sets);
WorldGenSets BuildDefaultWorldGenSets();
ObjectFeatureConfig ResolveObjectFeatures(const WorldGenSets &sets,
                                          const ObjectFeatureConfig &global);
bool ValidateWorldGenSets(const WorldGenSets &sets, std::string &error);

} // namespace cutum
