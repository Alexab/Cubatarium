#pragma once

#include "Render/Engine/TerrainGpuBuffers.h"
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <utility>

namespace cutum
{
inline nlohmann::json DescribeGpuBufferRoles(const TerrainGpuBufferSnapshot &snapshot,
                                            const char *schema, std::size_t first, std::size_t end)
{
  using Json = nlohmann::json;
  auto roles = Json::object();
  for (std::size_t index = first; index < end; ++index)
  {
    const auto &role = snapshot.roles[index];
    roles[TerrainGpuBufferRoleNames[index]] = {{"objects", role.objects}, {"logical_bytes", role.bytes}};
  }
  const char *status = !snapshot.contextAvailable ? "UNOBSERVED" :
      snapshot.failedQueries || snapshot.overflow ? "PARTIAL" : "PASS";
  return {{"schema", schema}, {"status", status},
      {"unique_objects", snapshot.total.objects}, {"logical_bytes", snapshot.total.bytes},
      {"references", snapshot.references}, {"names_without_object", snapshot.namesWithoutObject},
      {"failed_queries", snapshot.failedQueries}, {"shared_across_roles", snapshot.sharedAcrossRoles},
      {"overflow", snapshot.overflow}, {"roles", std::move(roles)}, {"whole_runtime_complete", false}};
}

inline nlohmann::json DescribeTerrainGpuBuffers(const TerrainGpuBufferSnapshot &snapshot)
{
  return DescribeGpuBufferRoles(snapshot, "terrain-gpu-buffers.v1", 0,
                                static_cast<std::size_t>(TerrainGpuBufferRole::GuiColored));
}

inline nlohmann::json DescribeGuiGpuBuffers(const TerrainGpuBufferSnapshot &snapshot)
{
  return DescribeGpuBufferRoles(snapshot, "gui-gpu-buffers.v1",
                                static_cast<std::size_t>(TerrainGpuBufferRole::GuiColored),
                                static_cast<std::size_t>(TerrainGpuBufferRole::PreviewCubeVertices));
}

inline nlohmann::json DescribePreviewGpuBuffers(const TerrainGpuBufferSnapshot &snapshot)
{
  return DescribeGpuBufferRoles(snapshot, "preview-gpu-buffers.v1",
                                static_cast<std::size_t>(TerrainGpuBufferRole::PreviewCubeVertices),
                                static_cast<std::size_t>(TerrainGpuBufferRole::InventoryIconReadback));
}

inline nlohmann::json DescribeIconReadbackGpuBuffers(const TerrainGpuBufferSnapshot &snapshot)
{
  return DescribeGpuBufferRoles(snapshot, "icon-readback-gpu-buffers.v1",
                                static_cast<std::size_t>(TerrainGpuBufferRole::InventoryIconReadback),
                                static_cast<std::size_t>(TerrainGpuBufferRole::CreaturePartVertices));
}

inline nlohmann::json DescribeActorGpuBuffers(const TerrainGpuBufferSnapshot &snapshot)
{
  return DescribeGpuBufferRoles(snapshot, "actor-gpu-buffers.v1",
                                static_cast<std::size_t>(TerrainGpuBufferRole::CreaturePartVertices),
                                static_cast<std::size_t>(TerrainGpuBufferRole::GeometryCubeVertices));
}

inline nlohmann::json DescribeGeometryGpuBuffers(const TerrainGpuBufferSnapshot &snapshot)
{
  return DescribeGpuBufferRoles(snapshot, "geometry-gpu-buffers.v1",
                                static_cast<std::size_t>(TerrainGpuBufferRole::GeometryCubeVertices), snapshot.roles.size());
}

inline nlohmann::json DescribeSelectedGpuBuffers(std::initializer_list<const TerrainGpuBufferCensus *> groups)
{
  TerrainGpuBufferCensus joined;
  for (const auto *group : groups)
    joined.Include(*group);
  const auto snapshot = joined.Read();
  return DescribeGpuBufferRoles(snapshot, "selected-gpu-buffers.v1", 0, snapshot.roles.size());
}
} // namespace cutum
