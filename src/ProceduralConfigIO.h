#pragma once

#include "ProceduralSettings.h"
#include "UiSettings.h"
#include <nlohmann/json_fwd.hpp>

namespace cutum
{

ProceduralSettings ParseProceduralSettings(const nlohmann::json &root);
void WriteProceduralSettings(nlohmann::json &root,
                             const ProceduralSettings &settings);
void WriteUiSettings(nlohmann::json &root, const UiSettings &settings);

} // namespace cutum
