#pragma once

#include "Render/Engine/TerrainGpuBuffers.h"
#include <nlohmann/json.hpp>
#include <utility>

namespace cutum
{
inline nlohmann::json DescribeTerrainGpuBuffers(const TerrainGpuBufferSnapshot &snapshot)
{
  using Json = nlohmann::json;
  auto roles = Json::object();
  for (std::size_t index = 0; index < snapshot.roles.size(); ++index)
  {
    const auto &role = snapshot.roles[index];
    roles[TerrainGpuBufferRoleNames[index]] = {{"objects", role.objects}, {"logical_bytes", role.bytes}};
  }
  const char *status = !snapshot.contextAvailable ? "UNOBSERVED" :
      snapshot.failedQueries || snapshot.overflow ? "PARTIAL" : "PASS";
  return {{"schema", "terrain-gpu-buffers.v1"}, {"status", status},
      {"unique_objects", snapshot.total.objects}, {"logical_bytes", snapshot.total.bytes},
      {"references", snapshot.references}, {"names_without_object", snapshot.namesWithoutObject},
      {"failed_queries", snapshot.failedQueries}, {"shared_across_roles", snapshot.sharedAcrossRoles},
      {"overflow", snapshot.overflow}, {"roles", std::move(roles)}, {"whole_runtime_complete", false}};
}
} // namespace cutum
