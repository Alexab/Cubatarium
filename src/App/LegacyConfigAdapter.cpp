#include "App/LegacyConfigAdapter.h"

#include "WorldGen/Core/ProceduralSettings.h"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

void ReadLegacyUiSettings(const nlohmann::json &ui, UiSettings &out)
{
  out.LegacyHud = ui.value("legacy_hud", false);
  out.ShowPerformance = ui.value("show_performance", true);
  out.PerfLogIntervalSec =
      std::clamp(ui.value("perf_log_interval_sec", 2.0f), 0.25f, 60.0f);
  out.ConsoleKey = ui.value("console_key", "grave");
  out.PaletteKey = ui.value("palette_key", "b");
  out.WorldGenKey = ui.value("worldgen_key", "g");
  out.InventoryKey = ui.value("inventory_key", "e");
  out.HotbarCount = std::clamp(ui.value("hotbar_count", 1), 1, 2);

  std::string schemeStr = "classic";
  if (ui.contains("control_scheme") && ui["control_scheme"].is_string())
  {
    schemeStr = ui["control_scheme"].get<std::string>();
  }
  else if (ui.contains("block_input_profile") &&
           ui["block_input_profile"].is_string())
  {
    schemeStr = ui["block_input_profile"].get<std::string>();
  }
  out.ControlScheme = ControlSchemeFromString(schemeStr);
  out.PlaceClickMaxSeconds = ui.value("place_click_max_seconds", 0.20f);
  out.BreakHoldMinSeconds = ui.value("break_hold_min_seconds", 0.50f);
  out.BreakDurationSeconds = ui.value("break_duration_seconds", 0.25f);
  out.RmbDragThresholdPx = ui.value("rmb_drag_threshold_px", 4);
  out.UiScaleUser = ui.value("ui_scale", 1.f);
}

void WriteLegacyUiSettings(nlohmann::json &ui, const UiSettings &settings)
{
  ui["legacy_hud"] = settings.LegacyHud;
  ui["show_performance"] = settings.ShowPerformance;
  ui["perf_log_interval_sec"] = settings.PerfLogIntervalSec;
  ui["console_key"] = settings.ConsoleKey;
  ui["palette_key"] = settings.PaletteKey;
  ui["worldgen_key"] = settings.WorldGenKey;
  ui["inventory_key"] = settings.InventoryKey;
  ui["hotbar_count"] = settings.HotbarCount;
  const std::string scheme = ControlSchemeToString(settings.ControlScheme);
  ui["control_scheme"] = scheme;
  ui["block_input_profile"] = scheme;
  ui["place_click_max_seconds"] = settings.PlaceClickMaxSeconds;
  ui["break_hold_min_seconds"] = settings.BreakHoldMinSeconds;
  ui["break_duration_seconds"] = settings.BreakDurationSeconds;
  ui["rmb_drag_threshold_px"] = settings.RmbDragThresholdPx;
  ui["ui_scale"] = settings.UiScaleUser;
}

ProceduralGenerator ReadLegacyTerrainGenerator(const nlohmann::json &root)
{
  if (!root.contains("terrain") || !root["terrain"].is_string())
  {
    return ProceduralGenerator::Heightmap;
  }
  const std::string terrain = root["terrain"].get<std::string>();
  if (terrain == "flat")
  {
    return ProceduralGenerator::Flat;
  }
  return ProceduralGeneratorFromString(terrain);
}

void WarnIfLegacyTerrainOverridden(const nlohmann::json &root,
                                   const ProceduralSettings &settings,
                                   bool hasProcedural)
{
  if (!hasProcedural || !root.contains("terrain") ||
      !root["terrain"].is_string())
  {
    return;
  }
  const std::string legacy = root["terrain"].get<std::string>();
  const std::string fromProc = ProceduralGeneratorToString(settings.Generator);
  if (legacy != fromProc)
  {
    std::cerr << "WARN: procedural.Generator (" << fromProc
              << ") overrides legacy terrain (" << legacy << ")" << std::endl;
  }
}

std::vector<std::string> ParseLegacyPackIdArray(const nlohmann::json &arr)
{
  std::vector<std::string> result;
  if (!arr.is_array())
  {
    return result;
  }
  result.reserve(arr.size());
  for (const auto &id : arr)
  {
    if (id.is_string())
    {
      result.push_back(id.get<std::string>());
    }
  }
  return result;
}

std::vector<std::string>
ReadLegacyEnabledPackList(const nlohmann::json &resourcePacksNode)
{
  if (!resourcePacksNode.is_object() ||
      !resourcePacksNode.contains("enabled") ||
      !resourcePacksNode["enabled"].is_array())
  {
    return {};
  }
  return ParseLegacyPackIdArray(resourcePacksNode["enabled"]);
}

std::vector<std::string>
ReadLegacyWorldEnabledPacks(const nlohmann::json &worldDataRoot)
{
  if (!worldDataRoot.contains("resource_packs") ||
      !worldDataRoot["resource_packs"].is_object())
  {
    return {};
  }
  return ReadLegacyEnabledPackList(worldDataRoot["resource_packs"]);
}

} // namespace cutum
