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
  ProceduralGenerator generator{ProceduralGenerator::Heightmap};
  VerticalMode vertical{VerticalMode::Compact};
  uint32_t seed{12345};
  int seaLevel{4};
  int maxHeight{12};
  int bedrockTopY{0};
  bool enableCaves{false};
  bool enableTrees{false};
  int flatSurfaceY{3};
  bool fillWater{false};
  bool fillLava{false};
  bool fillFire{false};
};

ProceduralGenerator ProceduralGeneratorFromString(const std::string &s);
const char *ProceduralGeneratorToString(ProceduralGenerator g);

VerticalMode VerticalModeFromString(const std::string &s);
const char *VerticalModeToString(VerticalMode m);

void ResolveProceduralDefaults(ProceduralSettings &s);
void ApplyGeneratorTierDefaults(ProceduralSettings &s);

} // namespace cutum
