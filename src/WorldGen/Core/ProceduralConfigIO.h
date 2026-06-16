#pragma once

#include "App/Settings/UiSettings.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <nlohmann/json_fwd.hpp>

namespace cutum
{

ProceduralSettings ParseProceduralSettings(const nlohmann::json &root);
void WriteProceduralSettings(nlohmann::json &root,
                             const ProceduralSettings &settings);
void WriteUiSettings(nlohmann::json &root, const UiSettings &settings);

} // namespace cutum
