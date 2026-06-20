#pragma once

#include <cstdint>
#include <string>

namespace cutum
{

enum class ProceduralGenerator
{
  Flat,
  Heightmap,
  Overworld,
  Hills,
  Mountains,
  OverworldBiomes,
  OverworldFull,
};

enum class VerticalMode
{
  Compact,
  Extended,
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
};

struct ProceduralSettings
{
  ProceduralGenerator Generator{ProceduralGenerator::OverworldBiomes};
  VerticalMode Vertical{VerticalMode::Extended};
  uint32_t Seed{12345};
  int SeaLevel{48};
  int MaxHeight{128};
  int BedrockTopY{0};
  bool EnableCaves{false};
  bool EnableTrees{true};
  int FlatSurfaceY{3};
  bool FillWater{false};
  bool FillLava{false};
  bool FillFire{false};
  WorldGenTuning Tuning;
};

float ClampTuningValue(float value);

ProceduralGenerator ProceduralGeneratorFromString(const std::string &s);
const char *ProceduralGeneratorToString(ProceduralGenerator g);

VerticalMode VerticalModeFromString(const std::string &s);
const char *VerticalModeToString(VerticalMode m);

void ResolveProceduralDefaults(ProceduralSettings &s);
void ApplyGeneratorTierDefaults(ProceduralSettings &s);
void ApplyGeneratorDescriptorDefaults(ProceduralSettings &s);
void ApplyVerticalModeDefaults(ProceduralSettings &s);
void ApplyGeneratorVerticalDefaults(ProceduralSettings &s);
void ResetToGeneratorDefaults(ProceduralSettings &s);

} // namespace cutum
