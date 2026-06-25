#include "WorldGen/Core/ProceduralConfigIO.h"
#include "WorldGen/Core/WorldGeneratorDescriptor.h"
#include "WorldGen/Core/WorldSeedParser.h"
#include "App/Settings/UiSettings.h"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

void ApplyLegacyVerticalMode(ProceduralSettings &settings,
                             const std::string &verticalMode,
                             bool hasSeaLevel, bool hasMaxHeight)
{
  if (hasSeaLevel || hasMaxHeight)
  {
    return;
  }
  if (verticalMode == "compact")
  {
    settings.SeaLevel = 5;
    settings.MaxHeight = 15;
    return;
  }
  if (verticalMode == "extended")
  {
    settings.SeaLevel = 48;
    settings.MaxHeight = 128;
    return;
  }
  if (!verticalMode.empty())
  {
    std::cerr << "WARN: unknown procedural.vertical '" << verticalMode
              << "', ignoring legacy field" << std::endl;
  }
}

void ApplyLegacyOverworldProfile(const std::string &legacyGenerator,
                                 ProceduralSettings &settings, bool hasCaves,
                                 bool hasTrees, bool hasOres, bool hasFillWater,
                                 bool hasFillLava, bool hasFillFire)
{
  if (settings.Generator != ProceduralGenerator::Overworld)
  {
    return;
  }
  if (legacyGenerator == "overworld_biomes")
  {
    if (!hasTrees)
    {
      settings.EnableTrees = true;
    }
    if (!hasCaves)
    {
      settings.EnableCaves = false;
    }
    if (!hasOres)
    {
      settings.EnableOres = false;
    }
    if (!hasFillWater)
    {
      settings.FillWater = true;
    }
    if (!hasFillLava)
    {
      settings.FillLava = true;
    }
    if (!hasFillFire)
    {
      settings.FillFire = true;
    }
  }
}

void ParseTuning(const nlohmann::json &tuning, WorldGenTuning &out)
{
  if (!tuning.is_object())
  {
    return;
  }
  if (tuning.contains("vegetation_density"))
  {
    out.vegetationDensity = tuning["vegetation_density"].get<float>();
  }
  if (tuning.contains("decoration_density"))
  {
    out.decorationDensity = tuning["decoration_density"].get<float>();
  }
  if (tuning.contains("structure_density"))
  {
    out.structureDensity = tuning["structure_density"].get<float>();
  }
  if (tuning.contains("biome_plains_weight"))
  {
    out.biomePlainsWeight = tuning["biome_plains_weight"].get<float>();
  }
  if (tuning.contains("biome_forest_weight"))
  {
    out.biomeForestWeight = tuning["biome_forest_weight"].get<float>();
  }
  if (tuning.contains("biome_desert_weight"))
  {
    out.biomeDesertWeight = tuning["biome_desert_weight"].get<float>();
  }
  if (tuning.contains("biome_hills_weight"))
  {
    out.biomeHillsWeight = tuning["biome_hills_weight"].get<float>();
  }
  if (tuning.contains("biome_tundra_weight"))
  {
    out.biomeTundraWeight = tuning["biome_tundra_weight"].get<float>();
  }
  if (tuning.contains("terrain_roughness"))
  {
    out.terrainRoughness = tuning["terrain_roughness"].get<float>();
  }
  if (tuning.contains("biome_blend_radius"))
  {
    out.biomeBlendRadius = tuning["biome_blend_radius"].get<float>();
  }
  if (tuning.contains("ore_density"))
  {
    out.oreDensity = tuning["ore_density"].get<float>();
  }
  if (tuning.contains("terrain_erosion"))
  {
    out.terrainErosion = tuning["terrain_erosion"].get<float>();
  }
  if (tuning.contains("river_width"))
  {
    out.riverWidth = tuning["river_width"].get<float>();
  }
  if (tuning.contains("thermal_erosion_iterations"))
  {
    out.thermalErosionIterations = tuning["thermal_erosion_iterations"].get<int>();
  }
  if (tuning.contains("hydraulic_erosion_iterations"))
  {
    out.hydraulicErosionIterations =
        tuning["hydraulic_erosion_iterations"].get<int>();
  }
  if (tuning.contains("erosion_strength"))
  {
    out.erosionStrength = tuning["erosion_strength"].get<float>();
  }
}

void ParseProceduralStreamingOptions(const nlohmann::json &p,
                                     ProceduralSettings &settings)
{
  if (!p.is_object())
  {
    return;
  }
  if (p.contains("async_chunk_generation"))
  {
    settings.AsyncChunkGeneration = p["async_chunk_generation"].get<bool>();
  }
  if (p.contains("async_chunk_io"))
  {
    settings.AsyncChunkIo = p["async_chunk_io"].get<bool>();
  }
  if (p.contains("max_chunk_commits_per_frame"))
  {
    settings.MaxChunkCommitsPerFrame =
        std::clamp(p["max_chunk_commits_per_frame"].get<int>(), 1, 32);
  }
  if (p.contains("max_load_ops_per_frame"))
  {
    settings.MaxLoadOpsPerFrame =
        std::clamp(p["max_load_ops_per_frame"].get<int>(), 1, 32);
  }
  if (p.contains("max_unload_ops_per_frame"))
  {
    settings.MaxUnloadOpsPerFrame =
        std::clamp(p["max_unload_ops_per_frame"].get<int>(), 1, 32);
  }
  if (p.contains("streaming") && p["streaming"].is_object())
  {
    ParseProceduralStreamingOptions(p["streaming"], settings);
  }
}

void WriteProceduralStreamingOptions(const ProceduralSettings &settings,
                                     nlohmann::json &procedural)
{
  procedural["async_chunk_generation"] = settings.AsyncChunkGeneration;
  procedural["async_chunk_io"] = settings.AsyncChunkIo;
  procedural["max_chunk_commits_per_frame"] = settings.MaxChunkCommitsPerFrame;
  procedural["max_load_ops_per_frame"] = settings.MaxLoadOpsPerFrame;
  procedural["max_unload_ops_per_frame"] = settings.MaxUnloadOpsPerFrame;
}

void WriteTuning(const WorldGenTuning &tuning, nlohmann::json &out)
{
  out["vegetation_density"] = tuning.vegetationDensity;
  out["decoration_density"] = tuning.decorationDensity;
  out["structure_density"] = tuning.structureDensity;
  out["biome_plains_weight"] = tuning.biomePlainsWeight;
  out["biome_forest_weight"] = tuning.biomeForestWeight;
  out["biome_desert_weight"] = tuning.biomeDesertWeight;
  out["biome_hills_weight"] = tuning.biomeHillsWeight;
  out["biome_tundra_weight"] = tuning.biomeTundraWeight;
  out["terrain_roughness"] = tuning.terrainRoughness;
  out["biome_blend_radius"] = tuning.biomeBlendRadius;
  out["ore_density"] = tuning.oreDensity;
  out["terrain_erosion"] = tuning.terrainErosion;
  out["river_width"] = tuning.riverWidth;
  out["thermal_erosion_iterations"] = tuning.thermalErosionIterations;
  out["hydraulic_erosion_iterations"] = tuning.hydraulicErosionIterations;
  out["erosion_strength"] = tuning.erosionStrength;
}

} // namespace

ProceduralSettings ParseProceduralSettings(const nlohmann::json &root)
{
  ProceduralSettings settings;
  if (root.contains("world_seed_text") && root["world_seed_text"].is_string())
  {
    const std::string seedText = root["world_seed_text"].get<std::string>();
    const WorldSeedResolution resolved = ResolveWorldSeed(seedText);
    settings.Seed = resolved.resolved;
    settings.SeedText = resolved.raw;
    settings.SeedKind = resolved.kind;
  }
  else
  {
    settings.Seed = root.value("world_seed", 12345u);
    settings.SeedText = std::to_string(settings.Seed);
    settings.SeedKind = WorldSeedKind::Numeric;
  }

  const bool hasProcedural =
      root.contains("procedural") && root["procedural"].is_object();
  if (hasProcedural)
  {
    const nlohmann::json &p = root["procedural"];
    std::string legacyGeneratorId;
    std::string legacyVerticalMode;
    bool hasSeaLevel = false;
    bool hasMaxHeight = false;
    bool hasCaves = false;
    bool hasTrees = false;
    bool hasDecoration = false;
    bool hasStructures = false;
    bool hasOres = false;
    bool hasFillWater = false;
    bool hasFillLava = false;
    bool hasFillFire = false;
    if (p.contains("generator") && p["generator"].is_string())
    {
      legacyGeneratorId = p["generator"].get<std::string>();
      settings.Generator =
          ProceduralGeneratorFromString(legacyGeneratorId);
    }
    if (p.contains("vertical") && p["vertical"].is_string())
    {
      legacyVerticalMode = p["vertical"].get<std::string>();
    }
    if (p.contains("sea_level"))
    {
      settings.SeaLevel = p["sea_level"].get<int>();
      hasSeaLevel = true;
    }
    if (p.contains("max_height"))
    {
      settings.MaxHeight = p["max_height"].get<int>();
      hasMaxHeight = true;
    }
    if (p.contains("bedrock_top_y"))
    {
      settings.BedrockTopY = p["bedrock_top_y"].get<int>();
    }
    if (p.contains("worldgen_pack_id") && p["worldgen_pack_id"].is_string())
    {
      settings.WorldGenPackId = p["worldgen_pack_id"].get<std::string>();
    }
    if (p.contains("preset") && p["preset"].is_string())
    {
      settings.WorldGenPresetId = p["preset"].get<std::string>();
    }
    if (p.contains("caves") && p["caves"].is_boolean())
    {
      settings.EnableCaves = p["caves"].get<bool>();
      hasCaves = true;
    }
    if (p.contains("enable_caves"))
    {
      settings.EnableCaves = p["enable_caves"].get<bool>();
      hasCaves = true;
    }
    if (p.contains("trees"))
    {
      settings.EnableTrees = p["trees"].get<bool>();
      hasTrees = true;
    }
    if (p.contains("decoration"))
    {
      settings.EnableDecoration = p["decoration"].get<bool>();
      hasDecoration = true;
    }
    if (p.contains("structures"))
    {
      settings.EnableStructures = p["structures"].get<bool>();
      hasStructures = true;
    }
    if (hasTrees && !hasDecoration)
    {
      settings.EnableDecoration = settings.EnableTrees;
    }
    if (hasTrees && !hasStructures)
    {
      settings.EnableStructures = settings.EnableTrees;
    }
    if (p.contains("flat_surface_y"))
    {
      settings.FlatSurfaceY = p["flat_surface_y"].get<int>();
    }
    if (p.contains("fill_water"))
    {
      settings.FillWater = p["fill_water"].get<bool>();
      hasFillWater = true;
    }
    if (p.contains("fill_lava"))
    {
      settings.FillLava = p["fill_lava"].get<bool>();
      hasFillLava = true;
    }
    if (p.contains("fill_fire"))
    {
      settings.FillFire = p["fill_fire"].get<bool>();
      hasFillFire = true;
    }
    if (p.contains("ores"))
    {
      settings.EnableOres = p["ores"].get<bool>();
      hasOres = true;
    }
    if (p.contains("cave_params") && p["cave_params"].is_object())
    {
      const nlohmann::json &caves = p["cave_params"];
      if (caves.contains("threshold"))
      {
        settings.Caves.threshold = caves["threshold"].get<float>();
      }
      if (caves.contains("min_y"))
      {
        settings.Caves.minY = caves["min_y"].get<int>();
      }
      if (caves.contains("max_depth_below_surface"))
      {
        settings.Caves.maxDepthBelowSurface =
            caves["max_depth_below_surface"].get<int>();
      }
      if (caves.contains("scale"))
      {
        settings.Caves.scale = caves["scale"].get<float>();
      }
      if (caves.contains("style") && caves["style"].is_string())
      {
        settings.Caves.style =
            CaveStyleFromString(caves["style"].get<std::string>());
      }
    }
    if (p.contains("tuning") && p["tuning"].is_object())
    {
      ParseTuning(p["tuning"], settings.Tuning);
    }
    ParseProceduralStreamingOptions(p, settings);
    ApplyLegacyVerticalMode(settings, legacyVerticalMode, hasSeaLevel,
                            hasMaxHeight);
    ApplyLegacyOverworldProfile(legacyGeneratorId, settings, hasCaves, hasTrees,
                                hasOres, hasFillWater, hasFillLava, hasFillFire);
  }
  else if (root.contains("terrain") && root["terrain"].is_string())
  {
    const std::string terrain = root["terrain"].get<std::string>();
    if (terrain == "flat")
    {
      settings.Generator = ProceduralGenerator::Flat;
    }
    else
    {
      settings.Generator = ProceduralGenerator::Heightmap;
    }
  }

  if (hasProcedural && root.contains("terrain") && root["terrain"].is_string())
  {
    const std::string legacy = root["terrain"].get<std::string>();
    const std::string fromProc =
        ProceduralGeneratorToString(settings.Generator);
    if (legacy != fromProc)
    {
      std::cerr << "WARN: procedural.Generator (" << fromProc
                << ") overrides legacy terrain (" << legacy << ")" << std::endl;
    }
  }

  ResolveProceduralDefaults(settings);
  ApplyGeneratorTierDefaults(settings);

  return settings;
}

ProceduralSettings ParseProceduralTemplateFromConfig(const nlohmann::json &root)
{
  ProceduralSettings settings;
  settings.Seed = root.value("world_seed", 12345u);
  std::string legacyGeneratorId;

  const bool hasProcedural =
      root.contains("procedural") && root["procedural"].is_object();
  if (hasProcedural)
  {
    const nlohmann::json &p = root["procedural"];
    if (p.contains("generator") && p["generator"].is_string())
    {
      legacyGeneratorId = p["generator"].get<std::string>();
      settings.Generator =
          ProceduralGeneratorFromString(legacyGeneratorId);
    }
  }
  else if (root.contains("terrain") && root["terrain"].is_string())
  {
    legacyGeneratorId = root["terrain"].get<std::string>();
    settings.Generator =
        ProceduralGeneratorFromString(legacyGeneratorId);
  }

  ResetToGeneratorDefaults(settings);
  ApplyLegacyOverworldProfile(legacyGeneratorId, settings, false, false, false,
                              false, false, false);
  if (hasProcedural)
  {
    ParseProceduralStreamingOptions(root["procedural"], settings);
  }
  return settings;
}

void WriteProceduralSettings(nlohmann::json &root,
                             const ProceduralSettings &settings)
{
  nlohmann::json procedural;
  procedural["generator"] = ProceduralGeneratorToString(settings.Generator);
  procedural["sea_level"] = settings.SeaLevel;
  procedural["max_height"] = settings.MaxHeight;
  procedural["bedrock_top_y"] = settings.BedrockTopY;
  procedural["worldgen_pack_id"] = settings.WorldGenPackId;
  procedural["preset"] = settings.WorldGenPresetId;
  procedural["caves"] = settings.EnableCaves;
  procedural["enable_caves"] = settings.EnableCaves;
  procedural["trees"] = settings.EnableTrees;
  procedural["decoration"] = settings.EnableDecoration;
  procedural["structures"] = settings.EnableStructures;
  procedural["flat_surface_y"] = settings.FlatSurfaceY;
  procedural["fill_water"] = settings.FillWater;
  procedural["fill_lava"] = settings.FillLava;
  procedural["fill_fire"] = settings.FillFire;
  procedural["ores"] = settings.EnableOres;
  nlohmann::json caveParams;
  caveParams["threshold"] = settings.Caves.threshold;
  caveParams["min_y"] = settings.Caves.minY;
  caveParams["max_depth_below_surface"] = settings.Caves.maxDepthBelowSurface;
  caveParams["scale"] = settings.Caves.scale;
  caveParams["style"] = CaveStyleToString(settings.Caves.style);
  procedural["cave_params"] = caveParams;
  nlohmann::json tuning;
  WriteTuning(settings.Tuning, tuning);
  procedural["tuning"] = tuning;
  WriteProceduralStreamingOptions(settings, procedural);
  root["procedural"] = procedural;
  root["terrain"] = ProceduralGeneratorToString(settings.Generator);
  root["world_seed"] = settings.Seed;
  if (!settings.SeedText.empty())
  {
    root["world_seed_text"] = settings.SeedText;
    root["world_seed_kind"] = WorldSeedKindToString(settings.SeedKind);
  }
}

void WriteProceduralTemplateConfig(nlohmann::json &root,
                                   const ProceduralSettings &settings)
{
  nlohmann::json procedural;
  procedural["generator"] = ProceduralGeneratorToString(settings.Generator);
  WriteProceduralStreamingOptions(settings, procedural);
  root["procedural"] = procedural;
  root["terrain"] = ProceduralGeneratorToString(settings.Generator);
  root["world_seed"] = settings.Seed;
  if (!settings.SeedText.empty())
  {
    root["world_seed_text"] = settings.SeedText;
    root["world_seed_kind"] = WorldSeedKindToString(settings.SeedKind);
  }
}

void WriteUiSettings(nlohmann::json &root, const UiSettings &settings)
{
  nlohmann::json ui;
  ui["legacy_hud"] = settings.LegacyHud;
  ui["show_performance"] = settings.ShowPerformance;
  ui["console_key"] = settings.ConsoleKey;
  ui["palette_key"] = settings.PaletteKey;
  ui["inventory_key"] = settings.InventoryKey;
  ui["hotbar_count"] = settings.HotbarCount;
  ui["control_scheme"] = ControlSchemeToString(settings.ControlScheme);
  ui["place_click_max_seconds"] = settings.PlaceClickMaxSeconds;
  ui["break_hold_min_seconds"] = settings.BreakHoldMinSeconds;
  ui["break_duration_seconds"] = settings.BreakDurationSeconds;
  ui["rmb_drag_threshold_px"] = settings.RmbDragThresholdPx;
  ui["ui_scale"] = settings.UiScaleUser;
  root["ui"] = ui;
}

} // namespace cutum
