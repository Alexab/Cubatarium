#pragma once

#include "ProceduralSettings.h"
#include <nlohmann/json_fwd.hpp>

namespace cutum {

ProceduralSettings ParseProceduralSettings(const nlohmann::json& root);
void WriteProceduralSettings(nlohmann::json& root, const ProceduralSettings& settings);

} // namespace cutum
