#include "WorldGen/Core/ProceduralConfigIO.h"
#include "WorldGen/Core/WorldGeneratorDescriptor.h"
#include "App/Settings/UiSettings.h"
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

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
}

} // namespace

ProceduralSettings ParseProceduralSettings(const nlohmann::json &root)
{
  ProceduralSettings settings;
  settings.Seed = root.value("world_seed", 12345u);

  const bool hasProcedural =
      root.contains("procedural") && root["procedural"].is_object();
  if (hasProcedural)
  {
    const nlohmann::json &p = root["procedural"];
    if (p.contains("generator") && p["generator"].is_string())
    {
      settings.Generator =
          ProceduralGeneratorFromString(p["generator"].get<std::string>());
    }
    if (p.contains("vertical") && p["vertical"].is_string())
    {
      settings.Vertical =
          VerticalModeFromString(p["vertical"].get<std::string>());
    }
    if (p.contains("sea_level"))
    {
      settings.SeaLevel = p["sea_level"].get<int>();
    }
    if (p.contains("max_height"))
    {
      settings.MaxHeight = p["max_height"].get<int>();
    }
    if (p.contains("caves") && p["caves"].is_boolean())
    {
      settings.EnableCaves = p["caves"].get<bool>();
    }
    if (p.contains("enable_caves"))
    {
      settings.EnableCaves = p["enable_caves"].get<bool>();
    }
    if (p.contains("trees"))
    {
      settings.EnableTrees = p["trees"].get<bool>();
    }
    if (p.contains("flat_surface_y"))
    {
      settings.FlatSurfaceY = p["flat_surface_y"].get<int>();
    }
    if (p.contains("fill_water"))
    {
      settings.FillWater = p["fill_water"].get<bool>();
    }
    if (p.contains("fill_lava"))
    {
      settings.FillLava = p["fill_lava"].get<bool>();
    }
    if (p.contains("fill_fire"))
    {
      settings.FillFire = p["fill_fire"].get<bool>();
    }
    if (p.contains("ores"))
    {
      settings.EnableOres = p["ores"].get<bool>();
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

  if (settings.Generator == ProceduralGenerator::OverworldFull)
  {
    if (hasProcedural)
    {
      const nlohmann::json &p = root["procedural"];
      if (p.contains("caves"))
      {
        settings.EnableCaves = p["caves"].get<bool>();
      }
      else
      {
        settings.EnableCaves = true;
      }
      if (p.contains("trees"))
      {
        settings.EnableTrees = p["trees"].get<bool>();
      }
      else
      {
        settings.EnableTrees = true;
      }
    }
    else
    {
      settings.EnableCaves = true;
      settings.EnableTrees = true;
    }
  }

  return settings;
}

ProceduralSettings ParseProceduralTemplateFromConfig(const nlohmann::json &root)
{
  ProceduralSettings settings;
  settings.Seed = root.value("world_seed", 12345u);

  const bool hasProcedural =
      root.contains("procedural") && root["procedural"].is_object();
  if (hasProcedural)
  {
    const nlohmann::json &p = root["procedural"];
    if (p.contains("generator") && p["generator"].is_string())
    {
      settings.Generator =
          ProceduralGeneratorFromString(p["generator"].get<std::string>());
    }
  }
  else if (root.contains("terrain") && root["terrain"].is_string())
  {
    settings.Generator =
        ProceduralGeneratorFromString(root["terrain"].get<std::string>());
  }

  ResetToGeneratorDefaults(settings);
  return settings;
}

void WriteProceduralSettings(nlohmann::json &root,
                             const ProceduralSettings &settings)
{
  nlohmann::json procedural;
  procedural["generator"] = ProceduralGeneratorToString(settings.Generator);
  procedural["vertical"] = VerticalModeToString(settings.Vertical);
  procedural["sea_level"] = settings.SeaLevel;
  procedural["max_height"] = settings.MaxHeight;
  procedural["caves"] = settings.EnableCaves;
  procedural["enable_caves"] = settings.EnableCaves;
  procedural["trees"] = settings.EnableTrees;
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
  root["procedural"] = procedural;
  root["terrain"] = ProceduralGeneratorToString(settings.Generator);
  root["world_seed"] = settings.Seed;
}

void WriteProceduralTemplateConfig(nlohmann::json &root,
                                   const ProceduralSettings &settings)
{
  nlohmann::json procedural;
  procedural["generator"] = ProceduralGeneratorToString(settings.Generator);
  root["procedural"] = procedural;
  root["terrain"] = ProceduralGeneratorToString(settings.Generator);
  root["world_seed"] = settings.Seed;
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
  root["ui"] = ui;
}

} // namespace cutum
