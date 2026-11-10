#ifndef CREATUREPERSISTENCEJSON_H
#define CREATUREPERSISTENCEJSON_H

#include "Creatures/Core/CreatureSpawnSourceJson.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace cutum
{

inline void WriteCreatureBodyOrigin(nlohmann::json &entity,
                                    const glm::vec3 &bodyOrigin)
{
  entity["body_origin"] =
      nlohmann::json::array({bodyOrigin.x, bodyOrigin.y, bodyOrigin.z});
}

inline glm::vec3 ReadCreatureBodyOrigin(const nlohmann::json &entity,
                                        const glm::vec3 &fallback)
{
  if (!entity.contains("body_origin") ||
      !entity["body_origin"].is_array() ||
      entity["body_origin"].size() != 3)
  {
    return fallback;
  }
  return glm::vec3(entity["body_origin"][0].get<float>(),
                   entity["body_origin"][1].get<float>(),
                   entity["body_origin"][2].get<float>());
}

} // namespace cutum

#endif
