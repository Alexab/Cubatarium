#pragma once

#include "App/Settings/UiSettings.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace cutum
{

/// Reads ui.* including legacy_hud and block_input_profile alias.
void ReadLegacyUiSettings(const nlohmann::json &ui, UiSettings &out);

/// Writes ui.* plus block_input_profile mirror for older configs.
void WriteLegacyUiSettings(nlohmann::json &ui, const UiSettings &settings);

/// Root-level terrain string when procedural block is absent (flat / heightmap).
ProceduralGenerator ReadLegacyTerrainGenerator(const nlohmann::json &root);

/// Warn when procedural.generator and legacy terrain disagree.
void WarnIfLegacyTerrainOverridden(const nlohmann::json &root,
                                   const ProceduralSettings &settings,
                                   bool hasProcedural);

/// Parses a JSON string array of pack ids (ignores non-string entries).
std::vector<std::string> ParseLegacyPackIdArray(const nlohmann::json &arr);

/// Reads resource_packs.enabled from config or world_data (legacy flat list).
std::vector<std::string>
ReadLegacyEnabledPackList(const nlohmann::json &resourcePacksNode);

/// Reads enabled packs from world_data.json root (empty when block absent).
std::vector<std::string>
ReadLegacyWorldEnabledPacks(const nlohmann::json &worldDataRoot);

} // namespace cutum
