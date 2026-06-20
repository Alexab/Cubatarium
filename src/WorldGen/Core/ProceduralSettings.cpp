#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGeneratorDescriptor.h"
#include <algorithm>
#include <iostream>

namespace cutum
{

float ClampTuningValue(float value)
{
  return std::clamp(value, 0.0f, 2.0f);
}

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
  if (s == "beta_retro")
  {
    return ProceduralGenerator::BetaRetro;
  }
  if (s == "indev_retro")
  {
    return ProceduralGenerator::IndevRetro;
  }
  std::cerr << "WARN: unknown procedural.Generator '" << s
            << "', using overworld_biomes" << std::endl;
  return ProceduralGenerator::OverworldBiomes;
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
  case ProceduralGenerator::BetaRetro:
    return "beta_retro";
  case ProceduralGenerator::IndevRetro:
    return "indev_retro";
  default:
    return "overworld_biomes";
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
              << "', using extended" << std::endl;
  }
  return VerticalMode::Compact;
}

const char *VerticalModeToString(VerticalMode m)
{
  return m == VerticalMode::Extended ? "extended" : "compact";
}

CaveStyle CaveStyleFromString(const std::string &s)
{
  if (s == "worm")
  {
    return CaveStyle::Worm;
  }
  return CaveStyle::Noise;
}

const char *CaveStyleToString(CaveStyle style)
{
  return style == CaveStyle::Worm ? "worm" : "noise";
}

namespace
{

void ClampTuning(WorldGenTuning &t)
{
  t.vegetationDensity = ClampTuningValue(t.vegetationDensity);
  t.decorationDensity = ClampTuningValue(t.decorationDensity);
  t.structureDensity = ClampTuningValue(t.structureDensity);
  t.biomePlainsWeight = ClampTuningValue(t.biomePlainsWeight);
  t.biomeForestWeight = ClampTuningValue(t.biomeForestWeight);
  t.biomeDesertWeight = ClampTuningValue(t.biomeDesertWeight);
  t.biomeHillsWeight = ClampTuningValue(t.biomeHillsWeight);
  t.biomeTundraWeight = ClampTuningValue(t.biomeTundraWeight);
  t.terrainRoughness = ClampTuningValue(t.terrainRoughness);
  t.biomeBlendRadius = std::clamp(t.biomeBlendRadius, 0.0f, 16.0f);
  t.oreDensity = ClampTuningValue(t.oreDensity);
  t.terrainErosion = std::clamp(t.terrainErosion, 0.0f, 1.0f);
  if (t.terrainRoughness < 0.25f)
  {
    t.terrainRoughness = 0.25f;
  }
}

void MigrateLegacyExtendedHeights(ProceduralSettings &s)
{
  if (s.Vertical != VerticalMode::Extended)
  {
    return;
  }
  // Pocket-era values saved under extended vertical (e.g. sea 10 / max 32).
  if (s.MaxHeight <= 32)
  {
    s.SeaLevel = 48;
    s.MaxHeight = 128;
  }
}

} // namespace

void ApplyVerticalModeDefaults(ProceduralSettings &s)
{
  if (s.Vertical == VerticalMode::Compact)
  {
    s.SeaLevel = 5;
    s.MaxHeight = 15;
  }
  else
  {
    s.SeaLevel = 48;
    s.MaxHeight = 128;
  }
}

void ApplyGeneratorVerticalDefaults(ProceduralSettings &s)
{
  switch (s.Generator)
  {
  case ProceduralGenerator::Flat:
  case ProceduralGenerator::Heightmap:
  case ProceduralGenerator::IndevRetro:
    s.Vertical = VerticalMode::Compact;
    break;
  default:
    s.Vertical = VerticalMode::Extended;
    break;
  }
  ApplyVerticalModeDefaults(s);
  if (s.Generator == ProceduralGenerator::Flat)
  {
    s.FlatSurfaceY = std::clamp(s.FlatSurfaceY > 0 ? s.FlatSurfaceY : 3, 1,
                                s.MaxHeight);
  }
}

void ResetToGeneratorDefaults(ProceduralSettings &s)
{
  const uint32_t seed = s.Seed;
  const ProceduralGenerator generator = s.Generator;
  s = ProceduralSettings{};
  s.Generator = generator;
  s.Seed = seed;
  if (const WorldGeneratorDescriptor *descriptor =
          UWorldGeneratorRegistry::Find(generator))
  {
    if (descriptor->ApplyDefaults)
    {
      descriptor->ApplyDefaults(s);
    }
  }
  else
  {
    ApplyGeneratorTierDefaults(s);
  }
  ApplyGeneratorVerticalDefaults(s);
  ResolveProceduralDefaults(s);
}

void ResolveProceduralDefaults(ProceduralSettings &s)
{
  MigrateLegacyExtendedHeights(s);
  if (s.Vertical == VerticalMode::Compact)
  {
    if (s.MaxHeight <= 0 || s.MaxHeight > 15)
    {
      s.MaxHeight = (s.MaxHeight > 15) ? 15 : 15;
    }
    if (s.SeaLevel <= 0)
    {
      s.SeaLevel = 5;
    }
    s.SeaLevel = std::min(s.SeaLevel, s.MaxHeight - 2);
    s.SeaLevel = std::max(s.SeaLevel, 2);
  }
  else
  {
    if (s.MaxHeight <= 0)
    {
      s.MaxHeight = 128;
    }
    s.MaxHeight = std::clamp(s.MaxHeight, 16, 128);
    if (s.SeaLevel <= 0)
    {
      s.SeaLevel = 48;
    }
    s.SeaLevel = std::clamp(s.SeaLevel, 4, s.MaxHeight - 4);
  }
  s.FlatSurfaceY = std::clamp(s.FlatSurfaceY, 1, s.MaxHeight);
  ClampTuning(s.Tuning);
}

void ApplyGeneratorTierDefaults(ProceduralSettings &s)
{
  if (s.Generator == ProceduralGenerator::OverworldBiomes ||
      s.Generator == ProceduralGenerator::OverworldFull ||
      s.Generator == ProceduralGenerator::Overworld ||
      s.Generator == ProceduralGenerator::Hills ||
      s.Generator == ProceduralGenerator::Mountains ||
      s.Generator == ProceduralGenerator::BetaRetro)
  {
    s.FillWater = true;
    s.FillFire = true;
  }
  if (s.Generator == ProceduralGenerator::OverworldBiomes ||
      s.Generator == ProceduralGenerator::OverworldFull ||
      s.Generator == ProceduralGenerator::BetaRetro)
  {
    s.EnableTrees = true;
  }
  if (s.Generator == ProceduralGenerator::OverworldFull)
  {
    s.EnableCaves = true;
    s.EnableOres = true;
  }
  if (s.Generator == ProceduralGenerator::BetaRetro)
  {
    s.FillWater = true;
    s.EnableTrees = true;
    s.Tuning.biomeHillsWeight = 0.6f;
    s.Tuning.vegetationDensity = 0.85f;
    s.Tuning.decorationDensity = 0.6f;
  }
}

void ApplyGeneratorDescriptorDefaults(ProceduralSettings &s)
{
  ResolveProceduralDefaults(s);
  ApplyGeneratorTierDefaults(s);
}

} // namespace cutum
