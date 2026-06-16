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

struct ProceduralSettings
{
  ProceduralGenerator Generator{ProceduralGenerator::Heightmap};
  VerticalMode Vertical{VerticalMode::Compact};
  uint32_t Seed{12345};
  int SeaLevel{4};
  int MaxHeight{12};
  int BedrockTopY{0};
  bool EnableCaves{false};
  bool EnableTrees{false};
  int FlatSurfaceY{3};
  bool FillWater{false};
  bool FillLava{false};
  bool FillFire{false};
};

ProceduralGenerator ProceduralGeneratorFromString(const std::string &s);
const char *ProceduralGeneratorToString(ProceduralGenerator g);

VerticalMode VerticalModeFromString(const std::string &s);
const char *VerticalModeToString(VerticalMode m);

void ResolveProceduralDefaults(ProceduralSettings &s);
void ApplyGeneratorTierDefaults(ProceduralSettings &s);

} // namespace cutum
