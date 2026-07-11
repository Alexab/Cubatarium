#include "WorldGen/Core/ProceduralConfigIO.h"
#include "App/LegacyConfigAdapter.h"
#include "App/Settings/UiSettings.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGeneratorDescriptor.h"
#include "WorldGen/Core/WorldSeedParser.h"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

void ApplyLegacyVerticalMode(ProceduralSettings &settings,
                             const std::string &verticalMode, bool hasSeaLevel,
                             bool hasMaxHeight)
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
  if (tuning.contains("biome_savanna_weight"))
  {
    out.biomeSavannaWeight = tuning["biome_savanna_weight"].get<float>();
  }
  if (tuning.contains("biome_foothills_weight"))
  {
    out.biomeFoothillsWeight = tuning["biome_foothills_weight"].get<float>();
  }
  if (tuning.contains("biome_scrubland_weight"))
  {
    out.biomeScrublandWeight = tuning["biome_scrubland_weight"].get<float>();
  }
  if (tuning.contains("biome_cold_steppe_weight"))
  {
    out.biomeColdSteppeWeight = tuning["biome_cold_steppe_weight"].get<float>();
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
  if (tuning.contains("erosion_strength"))
  {
    out.erosionStrength = tuning["erosion_strength"].get<float>();
  }
  if (tuning.contains("height_smoothing"))
  {
    out.heightSmoothing = tuning["height_smoothing"].get<bool>();
  }
  if (tuning.contains("height_smoothing_radius"))
  {
    out.heightSmoothingRadius = tuning["height_smoothing_radius"].get<int>();
  }
  if (tuning.contains("jitter_amplitude"))
  {
    out.jitterAmplitude = tuning["jitter_amplitude"].get<float>();
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
  if (p.contains("async_relight"))
  {
    settings.AsyncRelight = p["async_relight"].get<bool>();
  }
  if (p.contains("relight_thread_count"))
  {
    settings.RelightThreadCount =
        std::clamp(p["relight_thread_count"].get<int>(), 1, 8);
  }
  if (p.contains("ring_gate_enabled"))
  {
    settings.RingGateEnabled = p["ring_gate_enabled"].get<bool>();
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
  if (p.contains("max_chunk_commits_per_frame_boost"))
  {
    settings.MaxChunkCommitsPerFrameBoost =
        std::clamp(p["max_chunk_commits_per_frame_boost"].get<int>(), 1, 32);
  }
  if (p.contains("max_load_ops_per_frame_boost"))
  {
    settings.MaxLoadOpsPerFrameBoost =
        std::clamp(p["max_load_ops_per_frame_boost"].get<int>(), 1, 32);
  }
  if (p.contains("movement_speed_boost_threshold"))
  {
    settings.MovementSpeedBoostThreshold =
        p["movement_speed_boost_threshold"].get<float>();
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
  procedural["async_relight"] = settings.AsyncRelight;
  procedural["relight_thread_count"] = settings.RelightThreadCount;
  procedural["ring_gate_enabled"] = settings.RingGateEnabled;
  procedural["max_chunk_commits_per_frame"] = settings.MaxChunkCommitsPerFrame;
  procedural["max_load_ops_per_frame"] = settings.MaxLoadOpsPerFrame;
  procedural["max_unload_ops_per_frame"] = settings.MaxUnloadOpsPerFrame;
  procedural["max_chunk_commits_per_frame_boost"] =
      settings.MaxChunkCommitsPerFrameBoost;
  procedural["max_load_ops_per_frame_boost"] = settings.MaxLoadOpsPerFrameBoost;
  procedural["movement_speed_boost_threshold"] =
      settings.MovementSpeedBoostThreshold;
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
  out["biome_savanna_weight"] = tuning.biomeSavannaWeight;
  out["biome_foothills_weight"] = tuning.biomeFoothillsWeight;
  out["biome_scrubland_weight"] = tuning.biomeScrublandWeight;
  out["biome_cold_steppe_weight"] = tuning.biomeColdSteppeWeight;
  out["terrain_roughness"] = tuning.terrainRoughness;
  out["biome_blend_radius"] = tuning.biomeBlendRadius;
  out["ore_density"] = tuning.oreDensity;
  out["terrain_erosion"] = tuning.terrainErosion;
  out["river_width"] = tuning.riverWidth;
  out["erosion_strength"] = tuning.erosionStrength;
  out["height_smoothing"] = tuning.heightSmoothing;
  out["height_smoothing_radius"] = tuning.heightSmoothingRadius;
  out["jitter_amplitude"] = tuning.jitterAmplitude;
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
      settings.Generator = ProceduralGeneratorFromString(legacyGeneratorId);
    }
    {
      const uint32_t savedSeed = settings.Seed;
      const std::string savedSeedText = settings.SeedText;
      const WorldSeedKind savedSeedKind = settings.SeedKind;
      ResetToGeneratorDefaults(settings);
      settings.Seed = savedSeed;
      settings.SeedText = savedSeedText;
      settings.SeedKind = savedSeedKind;
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
      ApplyWorldGenPreset(settings, settings.WorldGenPresetId);
    }
    if (p.contains("terrain_backend") && p["terrain_backend"].is_string())
    {
      settings.TerrainBackendMode =
          TerrainBackendFromString(p["terrain_backend"].get<std::string>());
    }
    if (p.contains("caves") && p["caves"].is_boolean())
    {
      settings.EnableCaves = p["caves"].get<bool>();
      hasCaves = true;
    }
    if (p.contains("enable_caves"))
    {
      static bool warned = false;
      if (!warned)
      {
        std::cerr
            << "[config] procedural.enable_caves is deprecated; use caves\n";
        warned = true;
      }
      settings.EnableCaves = p["enable_caves"].get<bool>();
      hasCaves = true;
    }
    if (p.contains("trees"))
    {
      settings.EnableTrees = p["trees"].get<bool>();
      hasTrees = true;
    }
    if (p.contains("ground_cover"))
    {
      settings.EnableGroundCover = p["ground_cover"].get<bool>();
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
      if (caves.contains("use_density_field"))
      {
        settings.Caves.useDensityField = caves["use_density_field"].get<bool>();
      }
      if (caves.contains("density_cave_amplitude"))
      {
        settings.Caves.densityCaveAmplitude =
            caves["density_cave_amplitude"].get<float>();
      }
      if (caves.contains("chunk_gate_threshold"))
      {
        settings.Caves.chunkGateThreshold =
            caves["chunk_gate_threshold"].get<float>();
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
                                hasOres, hasFillWater, hasFillLava,
                                hasFillFire);
  }
  else if (root.contains("terrain") && root["terrain"].is_string())
  {
    settings.Generator = ReadLegacyTerrainGenerator(root);
  }

  if (hasProcedural)
  {
    WarnIfLegacyTerrainOverridden(root, settings, hasProcedural);
  }

  ResolveProceduralDefaults(settings);
  ApplyGeneratorTierDefaults(settings);

  return settings;
}

ProceduralSettings ParseProceduralTemplateFromConfig(const nlohmann::json &root)
{
  return ParseProceduralSettings(root);
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
  procedural["terrain_backend"] =
      TerrainBackendToString(settings.TerrainBackendMode);
  procedural["caves"] = settings.EnableCaves;
  procedural["trees"] = settings.EnableTrees;
  procedural["ground_cover"] = settings.EnableGroundCover;
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
  caveParams["use_density_field"] = settings.Caves.useDensityField;
  caveParams["density_cave_amplitude"] = settings.Caves.densityCaveAmplitude;
  caveParams["chunk_gate_threshold"] = settings.Caves.chunkGateThreshold;
  procedural["cave_params"] = caveParams;
  nlohmann::json tuning;
  WriteTuning(settings.Tuning, tuning);
  procedural["tuning"] = tuning;
  WriteProceduralStreamingOptions(settings, procedural);
  root["procedural"] = procedural;
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
  procedural["preset"] = settings.WorldGenPresetId;
  procedural["trees"] = settings.EnableTrees;
  procedural["ground_cover"] = settings.EnableGroundCover;
  procedural["decoration"] = settings.EnableDecoration;
  procedural["structures"] = settings.EnableStructures;
  nlohmann::json tuning;
  WriteTuning(settings.Tuning, tuning);
  procedural["tuning"] = tuning;
  WriteProceduralStreamingOptions(settings, procedural);
  root["procedural"] = procedural;
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
  WriteLegacyUiSettings(ui, settings);
  root["ui"] = ui;
}

} // namespace cutum
