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
    return ProceduralGenerator::Overworld;
  }
  if (s == "overworld_full")
  {
    return ProceduralGenerator::Overworld;
  }
  if (s == "beta_retro")
  {
    return ProceduralGenerator::BetaRetro;
  }
  if (s == "indev_retro")
  {
    std::cerr << "WARN: procedural.Generator 'indev_retro' is deprecated, "
                 "using heightmap"
              << std::endl;
    return ProceduralGenerator::Heightmap;
  }
  std::cerr << "WARN: unknown procedural.Generator '" << s
            << "', using overworld" << std::endl;
  return ProceduralGenerator::Overworld;
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
  case ProceduralGenerator::BetaRetro:
    return "beta_retro";
  default:
    return "overworld";
  }
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
  t.biomeSavannaWeight = ClampTuningValue(t.biomeSavannaWeight);
  t.biomeFoothillsWeight = ClampTuningValue(t.biomeFoothillsWeight);
  t.biomeScrublandWeight = ClampTuningValue(t.biomeScrublandWeight);
  t.biomeColdSteppeWeight = ClampTuningValue(t.biomeColdSteppeWeight);
  t.terrainRoughness = ClampTuningValue(t.terrainRoughness);
  t.biomeBlendRadius = std::clamp(t.biomeBlendRadius, 0.0f, 16.0f);
  t.oreDensity = ClampTuningValue(t.oreDensity);
  t.terrainErosion = std::clamp(t.terrainErosion, 0.0f, 1.0f);
  t.riverWidth = std::clamp(t.riverWidth, 0.5f, 1.5f);
  t.thermalErosionIterations = std::clamp(t.thermalErosionIterations, 0, 8);
  t.hydraulicErosionIterations = std::clamp(t.hydraulicErosionIterations, 0, 32);
  t.erosionStrength = std::clamp(t.erosionStrength, 0.0f, 1.0f);
  t.jitterAmplitude = std::clamp(t.jitterAmplitude, 0.0f, 2.0f);
  t.heightSmoothingRadius = std::clamp(t.heightSmoothingRadius, 0, 2);
  if (t.terrainRoughness < 0.25f)
  {
    t.terrainRoughness = 0.25f;
  }
}

void ApplyGeneratorHeightDefaults(ProceduralSettings &s)
{
  const bool compactGenerator =
      s.Generator == ProceduralGenerator::Flat ||
      s.Generator == ProceduralGenerator::Heightmap;
  if (compactGenerator)
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

} // namespace

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
    if (descriptor->PackId && descriptor->PackId[0] != '\0')
    {
      s.WorldGenPackId = descriptor->PackId;
    }
  }
  else
  {
    ApplyGeneratorTierDefaults(s);
  }
  ApplyGeneratorHeightDefaults(s);
  if (s.Generator == ProceduralGenerator::Flat)
  {
    s.FlatSurfaceY = std::clamp(s.FlatSurfaceY > 0 ? s.FlatSurfaceY : 3, 1,
                                s.MaxHeight);
  }
  ResolveProceduralDefaults(s);
}

void ResolveProceduralDefaults(ProceduralSettings &s)
{
  const bool compactGenerator =
      s.Generator == ProceduralGenerator::Flat ||
      s.Generator == ProceduralGenerator::Heightmap;
  if (compactGenerator)
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
  s.BedrockTopY = std::clamp(s.BedrockTopY, 0, s.MaxHeight);
  ClampTuning(s.Tuning);
}

void ApplyGeneratorTierDefaults(ProceduralSettings &s)
{
  if (s.Generator == ProceduralGenerator::Overworld ||
      s.Generator == ProceduralGenerator::Hills ||
      s.Generator == ProceduralGenerator::Mountains ||
      s.Generator == ProceduralGenerator::BetaRetro)
  {
    s.FillWater = true;
    s.FillFire = true;
  }
  if (s.Generator == ProceduralGenerator::Overworld ||
      s.Generator == ProceduralGenerator::BetaRetro)
  {
    s.EnableTrees = true;
    s.EnableGroundCover = true;
    s.FillLava = true;
  }
  if (s.Generator == ProceduralGenerator::Overworld)
  {
    s.EnableCaves = true;
    s.EnableOres = true;
    s.Caves.useDensityField = true;
    s.Caves.maxDepthBelowSurface = 48;
    s.Caves.chunkGateThreshold = 0.25f;
    s.Ravines.enabled = true;
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

void ApplyWorldGenPreset(ProceduralSettings &s, const std::string &presetId)
{
  s.WorldGenPresetId = presetId.empty() ? "balanced" : presetId;
  if (presetId == "realistic")
  {
    s.Tuning.terrainRoughness = 0.7f;
    s.Tuning.terrainErosion = 0.28f;
    s.Tuning.structureDensity = 0.25f;
    s.Tuning.vegetationDensity = 0.8f;
    s.Tuning.decorationDensity = 0.8f;
    s.Tuning.biomeBlendRadius = 16.0f;
    s.Tuning.hydraulicErosionIterations = 4;
    s.Tuning.erosionStrength = 0.25f;
    s.FillFire = false;
  }
  else if (presetId == "sparse_structures")
  {
    s.Tuning.structureDensity = 0.2f;
    s.Tuning.vegetationDensity = 0.75f;
    s.Tuning.decorationDensity = 0.75f;
  }
  else
  {
    s.WorldGenPresetId = "balanced";
    s.Tuning.terrainRoughness = 0.56f;
    s.Tuning.terrainErosion = 0.32f;
    s.Tuning.structureDensity = 0.35f;
    s.Tuning.vegetationDensity = 0.75f;
    s.Tuning.decorationDensity = 0.55f;
    s.Tuning.biomeBlendRadius = 14.0f;
    s.Tuning.thermalErosionIterations = 3;
    s.Tuning.hydraulicErosionIterations = 4;
    s.Tuning.erosionStrength = 0.25f;
    s.Tuning.heightSmoothing = true;
    s.Tuning.heightSmoothingRadius = 1;
    s.Tuning.thermalErosionIterations = 0;
    s.Tuning.hydraulicErosionIterations = 0;
    s.FillFire = false;
  }
  ClampTuning(s.Tuning);
}

} // namespace cutum
