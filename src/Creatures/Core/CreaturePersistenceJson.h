#ifndef CREATUREPERSISTENCEJSON_H
#define CREATUREPERSISTENCEJSON_H

#include "Creatures/Core/CreatureSpawnSourceJson.h"

#include "Creatures/Influence/StatusEffectTypes.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <vector>

namespace cutum
{

inline void
WriteCreatureEffects(nlohmann::json &j,
                     const std::vector<StatusEffectInstance> &effects)
{
  auto rows = nlohmann::json::array();
  for (const auto &s : effects)
    rows.push_back({{"id", s.DefId},
                    {"remaining", s.RemainingSec},
                    {"accumulator", s.TickAccumulator},
                    {"stacks", s.Stacks}});
  j["status_effects"] = rows;
}
inline void ReadCreatureEffects(const nlohmann::json &j,
                                std::vector<StatusEffectInstance> &effects)
{
  effects.clear();
  if (!j.contains("status_effects") || !j["status_effects"].is_array())
    return;
  for (const auto &r : j["status_effects"])
    try
    {
      StatusEffectInstance s;
      s.DefId = r.at("id").get<std::string>();
      s.RemainingSec = r.value("remaining", 0.f);
      s.TickAccumulator = r.value("accumulator", 0.f);
      s.Stacks = std::clamp(r.value("stacks", 1), 1, 100);
      if (std::isfinite(s.RemainingSec) && s.RemainingSec > 0 &&
          std::isfinite(s.TickAccumulator))
        effects.push_back(s);
    }
    catch (const nlohmann::json::exception &)
    {
    }
}

inline void WriteCreatureBodyOrigin(nlohmann::json &entity,
                                    const glm::vec3 &bodyOrigin)
{
  entity["body_origin"] =
      nlohmann::json::array({bodyOrigin.x, bodyOrigin.y, bodyOrigin.z});
}

inline glm::vec3 ReadCreatureBodyOrigin(const nlohmann::json &entity,
                                        const glm::vec3 &fallback)
{
  if (!entity.contains("body_origin") || !entity["body_origin"].is_array() ||
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
