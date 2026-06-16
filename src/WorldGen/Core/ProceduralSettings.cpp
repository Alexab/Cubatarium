#include "WorldGen/Core/ProceduralSettings.h"
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
  std::cerr << "WARN: unknown procedural.Generator '" << s
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
    std::cerr << "WARN: unknown procedural.Vertical '" << s
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
  if (s.Vertical == VerticalMode::Compact)
  {
    if (s.MaxHeight <= 0 || s.MaxHeight > 15)
    {
      if (s.MaxHeight > 15)
      {
        s.MaxHeight = 15;
      }
      else
      {
        s.MaxHeight = 12;
      }
    }
    if (s.SeaLevel <= 0)
    {
      s.SeaLevel = 4;
    }
    s.SeaLevel = std::min(s.SeaLevel, s.MaxHeight - 2);
    s.SeaLevel = std::max(s.SeaLevel, 2);
  }
  else
  {
    if (s.MaxHeight <= 0)
    {
      s.MaxHeight = 96;
    }
    s.MaxHeight = std::clamp(s.MaxHeight, 16, 128);
    if (s.SeaLevel <= 0)
    {
      s.SeaLevel = 32;
    }
    s.SeaLevel = std::clamp(s.SeaLevel, 4, s.MaxHeight - 4);
  }
  s.FlatSurfaceY = std::clamp(s.FlatSurfaceY, 1, s.MaxHeight);
}

void ApplyGeneratorTierDefaults(ProceduralSettings &s)
{
  if (s.Generator == ProceduralGenerator::OverworldBiomes ||
      s.Generator == ProceduralGenerator::OverworldFull ||
      s.Generator == ProceduralGenerator::Overworld)
  {
    s.FillWater = true;
    s.FillFire = true;
  }
}

} // namespace cutum
