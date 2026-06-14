#include "WorldGen/Core/ProceduralConfigIO.h"
#include "App/Settings/UiSettings.h"
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

ProceduralSettings ParseProceduralSettings(const nlohmann::json &root)
{
  ProceduralSettings settings;
  settings.seed = root.value("world_seed", 12345u);

  const bool hasProcedural =
      root.contains("procedural") && root["procedural"].is_object();
  if (hasProcedural)
  {
    const nlohmann::json &p = root["procedural"];
    if (p.contains("generator") && p["generator"].is_string())
    {
      settings.generator =
          ProceduralGeneratorFromString(p["generator"].get<std::string>());
    }
    if (p.contains("vertical") && p["vertical"].is_string())
    {
      settings.vertical =
          VerticalModeFromString(p["vertical"].get<std::string>());
    }
    if (p.contains("sea_level"))
    {
      settings.seaLevel = p["sea_level"].get<int>();
    }
    if (p.contains("max_height"))
    {
      settings.maxHeight = p["max_height"].get<int>();
    }
    if (p.contains("caves"))
    {
      settings.enableCaves = p["caves"].get<bool>();
    }
    if (p.contains("trees"))
    {
      settings.enableTrees = p["trees"].get<bool>();
    }
    if (p.contains("flat_surface_y"))
    {
      settings.flatSurfaceY = p["flat_surface_y"].get<int>();
    }
    if (p.contains("fill_water"))
    {
      settings.fillWater = p["fill_water"].get<bool>();
    }
    if (p.contains("fill_lava"))
    {
      settings.fillLava = p["fill_lava"].get<bool>();
    }
    if (p.contains("fill_fire"))
    {
      settings.fillFire = p["fill_fire"].get<bool>();
    }
  }
  else if (root.contains("terrain") && root["terrain"].is_string())
  {
    const std::string terrain = root["terrain"].get<std::string>();
    if (terrain == "flat")
    {
      settings.generator = ProceduralGenerator::Flat;
    }
    else
    {
      settings.generator = ProceduralGenerator::Heightmap;
    }
  }

  if (hasProcedural && root.contains("terrain") && root["terrain"].is_string())
  {
    const std::string legacy = root["terrain"].get<std::string>();
    const std::string fromProc =
        ProceduralGeneratorToString(settings.generator);
    if (legacy != fromProc)
    {
      std::cerr << "WARN: procedural.generator (" << fromProc
                << ") overrides legacy terrain (" << legacy << ")" << std::endl;
    }
  }

  ResolveProceduralDefaults(settings);
  ApplyGeneratorTierDefaults(settings);

  if (settings.generator == ProceduralGenerator::OverworldFull)
  {
    if (hasProcedural)
    {
      const nlohmann::json &p = root["procedural"];
      if (p.contains("caves"))
      {
        settings.enableCaves = p["caves"].get<bool>();
      }
      else
      {
        settings.enableCaves = true;
      }
      if (p.contains("trees"))
      {
        settings.enableTrees = p["trees"].get<bool>();
      }
      else
      {
        settings.enableTrees = true;
      }
    }
    else
    {
      settings.enableCaves = true;
      settings.enableTrees = true;
    }
  }

  return settings;
}

void WriteProceduralSettings(nlohmann::json &root,
                             const ProceduralSettings &settings)
{
  nlohmann::json procedural;
  procedural["generator"] = ProceduralGeneratorToString(settings.generator);
  procedural["vertical"] = VerticalModeToString(settings.vertical);
  procedural["sea_level"] = settings.seaLevel;
  procedural["max_height"] = settings.maxHeight;
  procedural["caves"] = settings.enableCaves;
  procedural["trees"] = settings.enableTrees;
  procedural["flat_surface_y"] = settings.flatSurfaceY;
  procedural["fill_water"] = settings.fillWater;
  procedural["fill_lava"] = settings.fillLava;
  procedural["fill_fire"] = settings.fillFire;
  root["procedural"] = procedural;
  root["terrain"] = ProceduralGeneratorToString(settings.generator);
  root["world_seed"] = settings.seed;
}

void WriteUiSettings(nlohmann::json &root, const UiSettings &settings)
{
  nlohmann::json ui;
  ui["legacy_hud"] = settings.legacyHud;
  ui["console_key"] = settings.consoleKey;
  ui["palette_key"] = settings.paletteKey;
  ui["inventory_key"] = settings.inventoryKey;
  ui["hotbar_count"] = settings.hotbarCount;
  ui["control_scheme"] = ControlSchemeToString(settings.controlScheme);
  ui["place_click_max_seconds"] = settings.placeClickMaxSeconds;
  ui["break_hold_min_seconds"] = settings.breakHoldMinSeconds;
  ui["break_duration_seconds"] = settings.breakDurationSeconds;
  ui["rmb_drag_threshold_px"] = settings.rmbDragThresholdPx;
  root["ui"] = ui;
}

} // namespace cutum
