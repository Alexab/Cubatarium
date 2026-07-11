#pragma once

#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Core/WorldGenStageId.h"
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

struct HeightLayerPackConfig
{
  float Scale{0.003f};
  int Octaves{2};
  float Weight{0.63f};
};

struct PackHeightConfig
{
  HeightLayerPackConfig Continental;
  HeightLayerPackConfig Regional;
  HeightLayerPackConfig Detail;
  HeightLayerPackConfig Rolling;
  float SeaBias{0.45f};
  float CurveExponent{1.12f};
  float JitterScale{0.03f};
  float JitterAmplitude{1.0f};
  float JitterErosionDamp{0.85f};
  bool Loaded{false};
};

struct ClimateAxisPackConfig
{
  float Scale{0.002f};
  int Octaves{3};
  int SeedOffset{0};
};

struct PackClimateConfig
{
  ClimateAxisPackConfig Temperature;
  ClimateAxisPackConfig Moisture;
  ClimateAxisPackConfig Continentalness;
  ClimateAxisPackConfig Erosion;
  ClimateAxisPackConfig Weirdness;
  ClimateAxisPackConfig Ridge;
  bool Loaded{false};
};

struct PackOreRule
{
  std::string Slot;
  int YPeak{32};
  int YSpread{24};
  int VeinSize{4};
  float Rarity{0.1f};
  int MaxSurfaceOffset{5};
  bool BelowSeaLevel{false};
  int SeedModulo{0};
};

struct PackOresConfig
{
  std::vector<PackOreRule> Rules;
  bool Loaded{false};
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
  std::vector<WorldGenStageId> StageOrder;
};

struct WorldGenPackInfo
{
  std::string Id;
  std::string Description;
};

struct WorldGenPack
{
  std::string Id{"default"};
  WorldGenBiomeMode BiomeMode{WorldGenBiomeMode::Procedural};
  std::string BiomeMapImagePath;
  int BiomeMapBlockScale{4};
  float BiomeBlendRadius{-1.0f};
  WorldGenPackPipeline Pipeline;
  PackHeightConfig Height;
  PackClimateConfig Climate;
  PackOresConfig Ores;
  std::unordered_map<std::string, BiomePackDefinition> Biomes;
};

class UWorldGenPack
{
public:
  static bool LoadFromDirectory(const std::string &packDir);
  static bool LoadPackId(const std::string &packId);
  static bool ReloadActive();
  static std::vector<std::string> ListPackIds();
  static std::vector<WorldGenPackInfo> ListPackInfos();
  static const WorldGenPack &Get();
  static const PackHeightConfig &HeightConfig();
  static const PackClimateConfig &ClimateConfig();
  static const PackOresConfig &OresConfig();
  static const BiomeHeightProfile *HeightProfileFor(const std::string &biomeId);
  static const BiomePackDefinition *BiomeDefinitionFor(const std::string &biomeId);
  static float FeatureWeightMultiplier(const std::string &biomeId,
                                       const std::string &prefabName);
  static float SubBiomePoolWeightMultiplier(const std::string &biomeId,
                                            SubBiomeId subBiome,
                                            ObjectFeaturePool pool);
  static BiomeId BiomeAtImage(int worldX, int worldZ);

private:
  static WorldGenPack ActivePack;
  static std::string ActivePackDir;
};

} // namespace cutum
