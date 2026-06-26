#pragma once

#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/BiomeRegistry.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cutum
{

enum class PrefabFeaturePool
{
  Vegetation,
  GroundCover,
  Decoration,
  Structures
};

enum class PrefabPlacementMode
{
  Prefab,
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
  std::vector<SubBiomeId> SubBiomes;
  PrefabPlacementMode Mode{PrefabPlacementMode::Prefab};
  ScatterBlockSpec Scatter;
  SurfaceConstraint Surface;
};

struct PrefabFeatureConfig
{
  std::vector<PrefabFeatureRule> Vegetation;
  std::vector<PrefabFeatureRule> GroundCover;
  std::vector<PrefabFeatureRule> Decoration;
  std::vector<PrefabFeatureRule> Structures;
  int structureCellSize{64};
  int structureChancePerCell{12};
  int structureMinSpacing{128};
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

SubBiomeId SubBiomeIdFromString(const std::string &name);
const char *SubBiomeIdToString(SubBiomeId subBiome);
SurfaceConstraintKind SurfaceConstraintKindFromString(const std::string &name);
const char *SurfaceConstraintKindToString(SurfaceConstraintKind kind);

} // namespace cutum
