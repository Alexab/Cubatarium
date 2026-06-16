#include "WorldGen/Core/ProceduralConfigIO.h"
#include "App/Settings/UiSettings.h"
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

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
    if (p.contains("caves"))
    {
      settings.EnableCaves = p["caves"].get<bool>();
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

void WriteProceduralSettings(nlohmann::json &root,
                             const ProceduralSettings &settings)
{
  nlohmann::json procedural;
  procedural["generator"] = ProceduralGeneratorToString(settings.Generator);
  procedural["vertical"] = VerticalModeToString(settings.Vertical);
  procedural["sea_level"] = settings.SeaLevel;
  procedural["max_height"] = settings.MaxHeight;
  procedural["caves"] = settings.EnableCaves;
  procedural["trees"] = settings.EnableTrees;
  procedural["flat_surface_y"] = settings.FlatSurfaceY;
  procedural["fill_water"] = settings.FillWater;
  procedural["fill_lava"] = settings.FillLava;
  procedural["fill_fire"] = settings.FillFire;
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
