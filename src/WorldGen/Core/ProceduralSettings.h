#pragma once

#include <cstdint>
#include <string>

#include "WorldGen/Features/CaveCarver.h"

namespace cutum
{

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
  float vegetationDensity{0.85f};
  float decorationDensity{1.0f};
  float structureDensity{0.35f};
  float biomePlainsWeight{1.0f};
  float biomeForestWeight{1.0f};
  float biomeDesertWeight{1.0f};
  float biomeHillsWeight{1.0f};
  float biomeTundraWeight{1.0f};
  float terrainRoughness{0.75f};
  float biomeBlendRadius{14.0f};
  float oreDensity{1.0f};
  float terrainErosion{0.22f};
  float riverWidth{1.0f};
  int thermalErosionIterations{2};
  int hydraulicErosionIterations{8};
  float erosionStrength{0.35f};
};

struct RavineParams
{
  bool enabled{true};
  int rarity{600};
  int minDepth{8};
  int maxDepth{40};
  bool fillWater{false};
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
  bool EnableDecoration{true};
  bool EnableStructures{true};
  bool EnableOres{false};
  bool AsyncChunkGeneration{true};
  bool AsyncChunkIo{true};
  int MaxChunkCommitsPerFrame{3};
  int MaxLoadOpsPerFrame{4};
  int MaxUnloadOpsPerFrame{2};
  int FlatSurfaceY{3};
  bool FillWater{false};
  bool FillLava{false};
  bool FillFire{false};
  CaveParams Caves;
  RavineParams Ravines;
  WorldGenTuning Tuning;
};

float ClampTuningValue(float value);

ProceduralGenerator ProceduralGeneratorFromString(const std::string &s);
const char *ProceduralGeneratorToString(ProceduralGenerator g);

CaveStyle CaveStyleFromString(const std::string &s);
const char *CaveStyleToString(CaveStyle style);

void ResolveProceduralDefaults(ProceduralSettings &s);
void ApplyGeneratorTierDefaults(ProceduralSettings &s);
void ApplyGeneratorDescriptorDefaults(ProceduralSettings &s);
void ResetToGeneratorDefaults(ProceduralSettings &s);
void ApplyWorldGenPreset(ProceduralSettings &s, const std::string &presetId);

} // namespace cutum
