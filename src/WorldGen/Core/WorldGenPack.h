#pragma once

#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Core/WorldGenStageId.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

struct ProceduralSettings;

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

struct PackCavesConfig
{
  int MaxDepthBelowSurface{48};
  int MinDepthBelowSurface{2};
  float ChunkGateThreshold{0.25f};
  bool UseDensityField{true};
  float DensityCaveAmplitude{0.15f};
  float CheeseScale{0.015f};
  float CheeseWeight{1.0f};
  float SpaghettiScale{0.045f};
  float SpaghettiWeight{0.55f};
  float NoodleScale{0.08f};
  float NoodleWeight{0.30f};
  bool Loaded{false};
};

struct PackRavinesConfig
{
  bool Enabled{true};
  int Rarity{600};
  int MinDepth{8};
  int MaxDepth{40};
  int AquaticMaxDepth{5};
  bool FillWater{false};
  std::string FeatherMode{"smoothstep"};
  bool Loaded{false};
};

struct PackValleysConfig
{
  bool Enabled{true};
  int MaxDepth{12};
  float WidthSigma{2.5f};
  float AquaticDepthScale{0.4f};
  float RiverNoiseScale{0.008f};
  bool Loaded{false};
};

struct WorldGenPackPipeline
{
  bool Loaded{false};
  bool Fluids{false};
  bool Ravines{false};
  bool Valleys{false};
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
  PackCavesConfig Caves;
  PackRavinesConfig Ravines;
  PackValleysConfig Valleys;
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
  /// Prefer GetSnapshot() for cross-thread / long-lived readers.
  static const WorldGenPack &Get();
  static std::shared_ptr<const WorldGenPack> GetSnapshot();
  static const PackHeightConfig &HeightConfig();
  static const PackClimateConfig &ClimateConfig();
  static const PackOresConfig &OresConfig();
  static const PackCavesConfig &CavesConfig();
  static const PackRavinesConfig &RavinesConfig();
  static const PackValleysConfig &ValleysConfig();
  static void ApplyPackCaveDefaults(ProceduralSettings &settings);
  static void ApplyPackRavineDefaults(ProceduralSettings &settings);
  static const BiomeHeightProfile *HeightProfileFor(const std::string &biomeId);
  static const BiomePackDefinition *BiomeDefinitionFor(const std::string &biomeId);
  static float FeatureWeightMultiplier(const std::string &biomeId,
                                       const std::string &prefabName);
  static float SubBiomePoolWeightMultiplier(const std::string &biomeId,
                                            SubBiomeId subBiome,
                                            ObjectFeaturePool pool);
  static BiomeId BiomeAtImage(int worldX, int worldZ);

private:
  static void Publish(std::shared_ptr<const WorldGenPack> pack);
  // Published via atomic load/store for cross-thread readers.
  static std::shared_ptr<const WorldGenPack> Active;
  static std::string ActivePackDir;
};

} // namespace cutum
