#pragma once

#include "App/Settings/UiSettings.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <nlohmann/json_fwd.hpp>
#include <string>

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

} // namespace cutum
