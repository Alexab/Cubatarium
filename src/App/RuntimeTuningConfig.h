#pragma once

#include <nlohmann/json_fwd.hpp>

namespace cutum
{

void ApplyRuntimeTuningFromConfig(const nlohmann::json *physics,
                                  const nlohmann::json *render,
                                  const nlohmann::json *procedural);

} // namespace cutum
