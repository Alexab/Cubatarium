#include "ProceduralSettings.h"
#include <algorithm>
#include <iostream>

namespace cutum
{

ProceduralGenerator ProceduralGeneratorFromString(const std::string &s)
{
  if (s == "flat")
  {
    return ProceduralGenerator::Flat;
  }
  if (s == "heightmap")
  {
    return ProceduralGenerator::Heightmap;
  }
  if (s == "overworld")
  {
    return ProceduralGenerator::Overworld;
  }
  if (s == "hills")
  {
    return ProceduralGenerator::Hills;
  }
  if (s == "mountains")
  {
    return ProceduralGenerator::Mountains;
  }
  if (s == "overworld_biomes")
  {
    return ProceduralGenerator::OverworldBiomes;
  }
  if (s == "overworld_full")
  {
    return ProceduralGenerator::OverworldFull;
  }
  std::cerr << "WARN: unknown procedural.generator '" << s
            << "', using heightmap" << std::endl;
  return ProceduralGenerator::Heightmap;
}

const char *ProceduralGeneratorToString(ProceduralGenerator g)
{
  switch (g)
  {
  case ProceduralGenerator::Flat:
    return "flat";
  case ProceduralGenerator::Heightmap:
    return "heightmap";
  case ProceduralGenerator::Overworld:
    return "overworld";
  case ProceduralGenerator::Hills:
    return "hills";
  case ProceduralGenerator::Mountains:
    return "mountains";
  case ProceduralGenerator::OverworldBiomes:
    return "overworld_biomes";
  case ProceduralGenerator::OverworldFull:
    return "overworld_full";
  default:
    return "heightmap";
  }
}

VerticalMode VerticalModeFromString(const std::string &s)
{
  if (s == "extended")
  {
    return VerticalMode::Extended;
  }
  if (s != "compact" && !s.empty())
  {
    std::cerr << "WARN: unknown procedural.vertical '" << s
              << "', using compact" << std::endl;
  }
  return VerticalMode::Compact;
}

const char *VerticalModeToString(VerticalMode m)
{
  return m == VerticalMode::Extended ? "extended" : "compact";
}

void ResolveProceduralDefaults(ProceduralSettings &s)
{
  if (s.vertical == VerticalMode::Compact)
  {
    if (s.maxHeight <= 0 || s.maxHeight > 15)
    {
      if (s.maxHeight > 15)
      {
        s.maxHeight = 15;
      }
      else
      {
        s.maxHeight = 12;
      }
    }
    if (s.seaLevel <= 0)
    {
      s.seaLevel = 4;
    }
    s.seaLevel = std::min(s.seaLevel, s.maxHeight - 2);
    s.seaLevel = std::max(s.seaLevel, 2);
  }
  else
  {
    if (s.maxHeight <= 0)
    {
      s.maxHeight = 96;
    }
    s.maxHeight = std::clamp(s.maxHeight, 16, 128);
    if (s.seaLevel <= 0)
    {
      s.seaLevel = 32;
    }
    s.seaLevel = std::clamp(s.seaLevel, 4, s.maxHeight - 4);
  }
  s.flatSurfaceY = std::clamp(s.flatSurfaceY, 1, s.maxHeight);
}

void ApplyGeneratorTierDefaults(ProceduralSettings &s)
{
  if (s.generator == ProceduralGenerator::OverworldBiomes ||
      s.generator == ProceduralGenerator::OverworldFull ||
      s.generator == ProceduralGenerator::Overworld)
  {
    s.fillWater = true;
    s.fillFire = true;
  }
}

} // namespace cutum
