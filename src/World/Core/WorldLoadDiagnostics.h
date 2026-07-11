#pragma once

#include <glm/glm.hpp>
#include <optional>
#include <string>

namespace cutum
{

class UWorld;

void LogWorldLoadDiag(const std::string &phase, const UWorld &world,
                      const std::optional<glm::vec3> &camera_pos = std::nullopt);

void WarnIfTerrainMeshesMissing(const UWorld &world, const std::string &context);

void WarnIfSpawnSkylightMissing(const UWorld &world, const std::string &context);

} // namespace cutum
