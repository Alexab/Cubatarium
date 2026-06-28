#pragma once

#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/BiomeRegistry.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cutum
{

enum class ObjectFeaturePool
{
  Vegetation,
  GroundCover,
  Decoration,
  Structures
};

enum class ObjectPlacementMode
{
  Object,
  ScatterBlocks,
};

enum class SurfaceConstraintKind
{
  AnyLand,
  Grass,
  NearWater,
  WaterSurface,
};

struct SurfaceConstraint
{
  SurfaceConstraintKind Kind{SurfaceConstraintKind::AnyLand};
  int NearWaterRadius{5};
};

struct ScatterBlockSpec
{
  std::string BlockName;
  int Attempts{4};
  int Radius{2};
  int DyOffset{0};
  int MaxPerChunk{0};
};

struct ObjectFeatureRule
{
  std::string ObjectName;
  std::vector<BiomeId> Biomes;
  int Spacing{0};
  int Weight{1};
  int ChancePerColumn{0};
  uint32_t SeedOffset{0};
  int PlacementYOffset{0};
  std::vector<SubBiomeId> SubBiomes;
  ObjectPlacementMode Mode{ObjectPlacementMode::Object};
  ScatterBlockSpec Scatter;
  SurfaceConstraint Surface;
};

struct ObjectFeatureConfig
{
  std::vector<ObjectFeatureRule> Vegetation;
  std::vector<ObjectFeatureRule> GroundCover;
  std::vector<ObjectFeatureRule> Decoration;
  std::vector<ObjectFeatureRule> Structures;
  int structureCellSize{64};
  int structureChancePerCell{12};
  int structureMinSpacing{128};
};

class UObjectFeatureConfigStorage
{
public:
  static bool LoadFromFile(const std::filesystem::path &path);
  static const ObjectFeatureConfig &Get();
  static bool IsLoaded();

private:
  static ObjectFeatureConfig Config;
  static bool Loaded;
};

SubBiomeId SubBiomeIdFromString(const std::string &name);
const char *SubBiomeIdToString(SubBiomeId subBiome);
SurfaceConstraintKind SurfaceConstraintKindFromString(const std::string &name);
const char *SurfaceConstraintKindToString(SurfaceConstraintKind kind);

} // namespace cutum
