#pragma once

#include "WorldGen/Sampling/BiomeRegistry.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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

enum class ObjectFeatureRuleMode
{
  Object,
  ScatterBlocks,
  ProceduralTree,
  ProceduralShrub,
};

enum class TreeCrownShape
{
  Round,
  Conical,
  Umbrella,
  Columnar,
};

struct TreeSpeciesProfile
{
  int HeightMin{5};
  int HeightMax{8};
  int CrownRadiusX{3};
  int CrownRadiusZ{3};
  int CrownHeight{4};
  int BranchCount{2};
  int CrownLobes{2};
  float LeafDensity{0.84f};
  float RootChance{0.45f};
  float CrownIrregularity{0.18f};
  TreeCrownShape CrownShape{TreeCrownShape::Round};
  std::string TrunkBlock{"tree_log"};
  std::string LeavesBlock{"tree_leaves"};
};

struct ShrubSpeciesProfile
{
  int HeightMin{2};
  int HeightMax{3};
  int RadiusX{2};
  int RadiusZ{2};
  int LobeCount{4};
  float LeafDensity{0.86f};
  float CrownIrregularity{0.22f};
  float AccentChance{0.0f};
  std::string StemBlock{"mtg_bush_stem"};
  std::string LeavesBlock{"mtg_bush_leaves"};
  std::string AccentBlock;
};

struct FeatureEcologyProfile
{
  enum class ElevationReference
  {
    AbsoluteY,
    SeaLevel,
  };

  bool HasTemperatureRange{false};
  bool HasMoistureRange{false};
  float TemperatureMin{0.0f};
  float TemperatureMax{1.0f};
  float MoistureMin{0.0f};
  float MoistureMax{1.0f};
  ElevationReference ElevationRef{ElevationReference::AbsoluteY};
  int MinElevation{std::numeric_limits<int>::min()};
  int MaxElevation{std::numeric_limits<int>::max()};
  float MaxSlope{0.0f};

  bool AllowsElevation(int worldY, int seaLevel) const
  {
    const int64_t origin =
        ElevationRef == ElevationReference::SeaLevel ? seaLevel : 0;
    return static_cast<int64_t>(worldY) >= origin + MinElevation &&
           static_cast<int64_t>(worldY) <= origin + MaxElevation;
  }
};

/// Runtime registry of authored feature behavior names. New algorithms add a
/// strategy type here and a matching placement strategy in the worldgen layer.
class ObjectFeatureTypeRegistry
{
public:
  static bool Register(const std::string &name, ObjectFeatureRuleMode mode);
  static std::optional<ObjectFeatureRuleMode> Find(const std::string &name);
};

enum class SurfaceConstraintKind
{
  AnyLand,
  Grass,
  NearWater,
  WaterSurface,
  UnderwaterFloor,
};

enum class CaveFeatureKind
{
  MossPatch,
  Stalagmite,
  AmethystCluster,
};

struct CaveFeatureRule
{
  BiomeId Biome{BiomeId::LushCaves};
  BiomeKey BiomeKeyName;
  CaveFeatureKind Kind{CaveFeatureKind::MossPatch};
  std::string BlockName;
  int Chance{96};
  int Radius{1};
  int MaxHeight{3};
  uint32_t SeedOffset{0};
};

class ICaveFeaturePlacementStrategy
{
public:
  virtual ~ICaveFeaturePlacementStrategy() = default;
  virtual bool Place(WorldGenContext &ctx, const CaveFeatureRule &rule,
                     glm::ivec3 floorPosition, BlockId featureBlock,
                     uint32_t featureHash, glm::ivec3 chunkCoordinate,
                     const std::function<bool(BlockId)> &isCaveRock) const = 0;
};

class CaveFeaturePlacementStrategyRegistry
{
public:
  static bool
  Register(CaveFeatureKind kind,
           std::shared_ptr<const ICaveFeaturePlacementStrategy> strategy);
  static std::shared_ptr<const ICaveFeaturePlacementStrategy>
  Find(CaveFeatureKind kind);
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
  // Keep the authored keys so pack-defined biomes survive parsing even before
  // every placement consumer has migrated from the legacy enum API.
  std::vector<BiomeKey> BiomeKeys;
  int Spacing{0};
  int Weight{1};
  int ChancePerColumn{0};
  uint32_t SeedOffset{0};
  int PlacementYOffset{0};
  std::vector<SubBiomeId> SubBiomes;
  ObjectFeatureRuleMode Mode{ObjectFeatureRuleMode::Object};
  ScatterBlockSpec Scatter;
  TreeSpeciesProfile Tree;
  ShrubSpeciesProfile Shrub;
  FeatureEcologyProfile Ecology;
  SurfaceConstraint Surface;
};

class IObjectFeaturePlacementStrategy
{
public:
  virtual ~IObjectFeaturePlacementStrategy() = default;
  virtual bool Place(WorldGenContext &ctx, int x, int z, int surfaceY,
                     const ObjectFeatureRule &rule) const = 0;
};

/// Runtime registry separates feature type names from their placement
/// algorithms. New modes can register an implementation without adding another
/// branch to the candidate-selection pipeline.
class ObjectFeaturePlacementStrategyRegistry
{
public:
  static bool
  Register(ObjectFeatureRuleMode mode,
           std::shared_ptr<const IObjectFeaturePlacementStrategy> strategy);
  static std::shared_ptr<const IObjectFeaturePlacementStrategy>
  Find(ObjectFeatureRuleMode mode);
};

struct ObjectFeatureConfig
{
  std::vector<ObjectFeatureRule> Vegetation;
  std::vector<ObjectFeatureRule> GroundCover;
  std::vector<ObjectFeatureRule> Decoration;
  std::vector<ObjectFeatureRule> Structures;
  std::vector<CaveFeatureRule> CaveFeatures;
  int structureCellSize{64};
  int structureChancePerCell{12};
  int structureMinSpacing{128};
};

class UObjectFeatureConfigStorage
{
public:
  static bool LoadFromFile(const std::filesystem::path &path);
  static const ObjectFeatureConfig &Get();
  static std::shared_ptr<const ObjectFeatureConfig> GetSnapshot();
  static bool IsLoaded();

private:
  static std::shared_ptr<const ObjectFeatureConfig> Active;
};

SubBiomeId SubBiomeIdFromString(const std::string &name);
const char *SubBiomeIdToString(SubBiomeId subBiome);
SurfaceConstraintKind SurfaceConstraintKindFromString(const std::string &name);
const char *SurfaceConstraintKindToString(SurfaceConstraintKind kind);

} // namespace cutum
