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
  float vegetationDensity{1.0f};
  float decorationDensity{1.0f};
  float structureDensity{1.0f};
  float biomePlainsWeight{1.0f};
  float biomeForestWeight{1.0f};
  float biomeDesertWeight{1.0f};
  float biomeHillsWeight{1.0f};
  float biomeTundraWeight{1.0f};
  float terrainRoughness{1.0f};
  float biomeBlendRadius{6.0f};
  float oreDensity{1.0f};
  float terrainErosion{0.0f};
};

struct ProceduralSettings
{
  ProceduralGenerator Generator{ProceduralGenerator::Overworld};
  uint32_t Seed{12345};
  int SeaLevel{48};
  int MaxHeight{128};
  int BedrockTopY{0};
  std::string WorldGenPackId{"default"};
  bool EnableCaves{false};
  bool EnableTrees{true};
  bool EnableOres{false};
  int FlatSurfaceY{3};
  bool FillWater{false};
  bool FillLava{false};
  bool FillFire{false};
  CaveParams Caves;
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

} // namespace cutum
