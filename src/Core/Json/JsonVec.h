#ifndef CORE_JSON_JSONVEC_H
#define CORE_JSON_JSONVEC_H

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace cutum
{

/// Shared JSON vector readers — six identical copies lived across creature,
/// item and preview parsers (audit 2.6).
inline glm::vec3 ReadVec3(const nlohmann::json &arr, const glm::vec3 &fallback)
{
  if (!arr.is_array() || arr.size() < 3)
  {
    return fallback;
  }
  return glm::vec3(arr[0].get<float>(), arr[1].get<float>(),
                   arr[2].get<float>());
}

inline glm::vec4 ReadVec4(const nlohmann::json &arr, const glm::vec4 &fallback)
{
  if (!arr.is_array() || arr.size() < 4)
  {
    return fallback;
  }
  return glm::vec4(arr[0].get<float>(), arr[1].get<float>(),
                   arr[2].get<float>(), arr[3].get<float>());
}

} // namespace cutum

#endif
