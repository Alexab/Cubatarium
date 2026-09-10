#pragma once

#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Streaming/WorkToken.h"
#include <glm/glm.hpp>

namespace cutum
{

/// Build dependency stamp for mesh capture at `coord`.
inline DependencyStamp BuildMeshCaptureDependencyStamp(
    const UBlockWorld &world, glm::ivec3 coord, uint64_t content_revision,
    const UBlockRegistry &registry)
{
  DependencyStamp stamp;
  stamp.content_revision = content_revision;
  stamp.material_catalog_revision =
      MaterialCatalogRevision(registry.GetDefinitionsCatalogSnapshot());
  if (const UChunk *chunk = world.GetChunkManager().GetChunk(coord))
  {
    stamp.light_revision = chunk->GetLightFieldRevision();
  }
  static constexpr std::array<glm::ivec3, 6> kNeighborDelta{
      glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),  glm::ivec3(0, 0, -1),
      glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0), glm::ivec3(0, 1, 0)};
  for (size_t i = 0; i < kNeighborDelta.size(); ++i)
  {
    const glm::ivec3 ncoord = coord + kNeighborDelta[i];
    if (const UChunk *neighbor = world.GetChunkManager().GetChunk(ncoord))
    {
      stamp.halo_light_revision[i] = neighbor->GetLightFieldRevision();
    }
  }
  return stamp;
}

inline DependencyStamp BuildRelightDependencyStamp(
    const UBlockWorld &world, glm::ivec3 chunk_coord,
    const UBlockRegistry &registry)
{
  return BuildMeshCaptureDependencyStamp(world, chunk_coord, 0, registry);
}

inline uint64_t ChunkIncarnationAt(const UBlockWorld &world, glm::ivec3 coord)
{
  (void)world;
  (void)coord;
  return 0;
}

/// Reject async results when live world diverged from capture-time stamp.
inline bool CaptureDependencyStillValid(const DependencyStamp &captured,
                                        const DependencyStamp &current)
{
  if (captured.content_revision != 0 &&
      captured.content_revision != current.content_revision)
  {
    return false;
  }
  if (captured.light_revision != current.light_revision)
  {
    return false;
  }
  if (captured.halo_light_revision != current.halo_light_revision)
  {
    return false;
  }
  if (captured.material_catalog_revision != 0 &&
      captured.material_catalog_revision != current.material_catalog_revision)
  {
    return false;
  }
  return true;
}

} // namespace cutum
