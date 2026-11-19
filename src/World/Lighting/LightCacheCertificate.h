#pragma once

#include "Blocks/BlockDefinitionStorage.h"
#include "World/Math/WorldVerticalProfile.h"
#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace cutum
{

// This guards global cache compatibility only. Per-section dependency
// settlement is a separate lifecycle obligation; a matching signature alone
// must never mark an unfinished section as complete.

// Increment when solver semantics change. Old bytes remain loadable terrain
// data, but must not be treated as a completed solution for a newer solver.
// v6 uses the geometry solver for synchronous and boundary paths as well.
// v5 derived asynchronous fields from geometry instead of neighboring light.
// v4 removed the successful section-seed bypass of full-height sky.
inline constexpr uint32_t kLightSolverCacheVersion = 6;

// Exact comparison of the material contract encoded by the cache signature.
// Used only to export an already-proven calculation after a catalog refresh;
// it does not replace runtime catalog identity or certify any input read set.
inline bool LightCacheCatalogsEquivalent(const BlockDefinitionCatalog &a,
                                         const BlockDefinitionCatalog &b)
{
  if (a.ById.size() != b.ById.size())
    return false;
  for (const auto &[id, old] : a.ById)
  {
    const auto found = b.ById.find(id);
    if (found == b.ById.end())
      return false;
    const auto &now = found->second;
    if (old.Name != now.Name || old.Physics.IsLiquid != now.Physics.IsLiquid ||
        (old.Physics.Movement.Occupancy >= 1.0f) !=
            (now.Physics.Movement.Occupancy >= 1.0f) ||
        old.Render.Transparent != now.Render.Transparent ||
        old.Render.Style != now.Render.Style ||
        std::clamp(old.Lighting.Emission, 0, 15) !=
            std::clamp(now.Lighting.Emission, 0, 15))
      return false;
  }
  return true;
}

inline std::string
MakeLightCacheSignature(const WorldVerticalProfile &profile,
                        const BlockDefinitionCatalog &catalog,
                        int lighting_mode)
{
  if (!profile.IsValid())
    return {};
  uint64_t hash = 14695981039346656037ull;
  const auto byte = [&](uint8_t value)
  {
    hash ^= value;
    hash *= 1099511628211ull;
  };
  const auto integer = [&](uint32_t value)
  {
    for (int shift = 0; shift < 32; shift += 8)
      byte(static_cast<uint8_t>(value >> shift));
  };
  std::vector<BlockId> ids;
  ids.reserve(catalog.ById.size());
  for (const auto &entry : catalog.ById)
    ids.push_back(entry.first);
  std::sort(ids.begin(), ids.end());
  for (const BlockId id : ids)
  {
    const auto &definition = catalog.ById.at(id);
    integer(id);
    integer(static_cast<uint32_t>(definition.Name.size()));
    for (const unsigned char ch : definition.Name)
      byte(ch);
    byte(definition.Physics.IsLiquid);
    byte(definition.Physics.Movement.Occupancy >= 1.0f);
    byte(definition.Render.Transparent);
    integer(static_cast<uint32_t>(definition.Render.Style));
    integer(
        static_cast<uint32_t>(std::clamp(definition.Lighting.Emission, 0, 15)));
  }
  return "light-v" + std::to_string(kLightSolverCacheVersion) + ":" +
         std::to_string(lighting_mode) + ":" + std::to_string(profile.MinY) +
         ":" + std::to_string(profile.MaxY) + ":" + std::to_string(hash);
}

inline nlohmann::json EncodeColumnLightCache(std::vector<glm::ivec2> columns,
                                             const std::string &signature)
{
  std::sort(columns.begin(), columns.end(), [](const auto &a, const auto &b)
            { return a.x < b.x || (a.x == b.x && a.y < b.y); });
  columns.erase(std::unique(columns.begin(), columns.end()), columns.end());
  nlohmann::json complete = nlohmann::json::array();
  for (const auto &column : columns)
    complete.push_back({column.x, column.y});
  return {{"format_version", 2},
          {"light_signature", signature},
          {"complete", std::move(complete)}};
}

inline std::optional<std::vector<glm::ivec2>>
DecodeColumnLightCache(const nlohmann::json &data,
                       const std::string &expected_signature)
{
  if (expected_signature.empty() || !data.is_object() ||
      !data.contains("format_version") ||
      !data["format_version"].is_number_integer() ||
      data["format_version"] != 2 || !data.contains("light_signature") ||
      data["light_signature"] != expected_signature ||
      !data.contains("complete") || !data["complete"].is_array())
    return std::nullopt;
  std::vector<glm::ivec2> result;
  for (const auto &entry : data["complete"])
  {
    if (!entry.is_array() || entry.size() != 2)
      return std::nullopt;
    glm::ivec2 column;
    for (int axis = 0; axis < 2; ++axis)
    {
      const auto &value = entry[axis];
      if (!value.is_number_integer())
        return std::nullopt;
      if (value.is_number_unsigned())
      {
        if (value.get<uint64_t>() >
            static_cast<uint64_t>(std::numeric_limits<int>::max()))
          return std::nullopt;
      }
      else if (value.get<int64_t>() < std::numeric_limits<int>::min() ||
               value.get<int64_t>() > std::numeric_limits<int>::max())
        return std::nullopt;
      column[axis] = value.get<int>();
    }
    result.push_back(column);
  }
  return result;
}

} // namespace cutum
