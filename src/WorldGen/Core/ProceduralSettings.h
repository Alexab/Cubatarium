#pragma once

#include <cstdint>
#include <string>

#include "WorldGen/Features/CaveCarver.h"

namespace cutum
{

enum class TerrainBackend
{
  Heightmap,
  Density3D,
};

enum class ProceduralGenerator
{
  Flat,
  Heightmap,
  Overworld,
  Hills,
  Mountains,
  BetaRetro,
};

struct WorldGenTuning
{
  float vegetationDensity{0.68f};
  float decorationDensity{0.85f};
  float structureDensity{0.35f};
  float biomePlainsWeight{1.0f};
  float biomeForestWeight{1.0f};
  float biomeDesertWeight{1.0f};
  float biomeHillsWeight{1.0f};
  float biomeTundraWeight{1.0f};
  float biomeSavannaWeight{1.0f};
  float biomeFoothillsWeight{1.0f};
  float biomeScrublandWeight{1.0f};
  float biomeColdSteppeWeight{1.0f};
  float terrainRoughness{0.58f};
  float biomeBlendRadius{14.0f};
  float oreDensity{1.0f};
  float terrainErosion{0.32f};
  float riverWidth{1.0f};
  float erosionStrength{0.25f};
  bool heightSmoothing{true};
  int heightSmoothingRadius{1};
  float jitterAmplitude{1.0f};
  bool useUnifiedHeightField{false};
  bool useAnalyticValleys{false};
  bool useMudflowErosion{false};
  bool useDensityRefineParity{false};
};

enum class RavineFeatherMode
{
  Smoothstep,
  Linear,
};

struct RavineParams
{
  bool enabled{true};
  int rarity{600};
  int minDepth{8};
  int maxDepth{40};
  int aquaticMaxDepth{5};
  bool fillWater{false};
  RavineFeatherMode featherMode{RavineFeatherMode::Smoothstep};
};

enum class WorldSeedKind
{
  Numeric,
  Hashed,
  Random,
};

enum class WorldSeedHashAlgo
{
  Fnv1a32,
  JavaStringHash,
};

struct ProceduralSettings
{
  ProceduralGenerator Generator{ProceduralGenerator::Overworld};
  uint32_t Seed{12345};
  std::string SeedText;
  WorldSeedKind SeedKind{WorldSeedKind::Numeric};
  WorldSeedHashAlgo SeedHashAlgo{WorldSeedHashAlgo::Fnv1a32};
  int SeaLevel{48};
  int MaxHeight{128};
  int BedrockTopY{0};
  std::string WorldGenPackId{"default"};
  std::string WorldGenPresetId{"balanced"};
  bool EnableCaves{false};
  bool EnableTrees{true};
  bool EnableGroundCover{true};
  bool EnableDecoration{true};
  bool EnableStructures{true};
  bool EnableOres{false};
  bool AsyncChunkGeneration{true};
  bool AsyncChunkIo{true};
  bool AsyncRelight{true};
  int RelightThreadCount{4};
  bool RingGateEnabled{false};
  int MaxChunkCommitsPerFrame{3};
  int MaxLoadOpsPerFrame{4};
  int MaxUnloadOpsPerFrame{2};
  int MaxChunkCommitsPerFrameBoost{6};
  int MaxLoadOpsPerFrameBoost{8};
  float MovementSpeedBoostThreshold{6.0f};
  int FlatSurfaceY{3};
  bool FillWater{false};
  bool FillLava{false};
  bool FillFire{false};
  bool DebugWorldGenOverlay{false};
  CaveParams Caves;
  RavineParams Ravines;
  WorldGenTuning Tuning;
  TerrainBackend TerrainBackendMode{TerrainBackend::Heightmap};
};

float ClampTuningValue(float value);

ProceduralGenerator ProceduralGeneratorFromString(const std::string &s);
const char *ProceduralGeneratorToString(ProceduralGenerator g);

CaveStyle CaveStyleFromString(const std::string &s);
const char *CaveStyleToString(CaveStyle style);

TerrainBackend TerrainBackendFromString(const std::string &s);
const char *TerrainBackendToString(TerrainBackend backend);

void ResolveProceduralDefaults(ProceduralSettings &s);
void ApplyGeneratorTierDefaults(ProceduralSettings &s);
void ApplyGeneratorDescriptorDefaults(ProceduralSettings &s);
void ResetToGeneratorDefaults(ProceduralSettings &s);
void ApplyWorldGenPreset(ProceduralSettings &s, const std::string &presetId);

} // namespace cutum
