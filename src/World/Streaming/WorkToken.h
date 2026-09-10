#pragma once

#include "World/Chunks/ChunkManager.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>

namespace cutum
{

struct BlockDefinitionCatalog;

/// Work identity for async mesh / relight jobs (M09 / A07).
enum class WorkDomain : uint8_t
{
  MeshCapture = 0,
  MeshBuild = 1,
  Relight = 2,
};

struct WorkToken
{
  uint64_t world_epoch{0};
  glm::ivec3 coord{0};
  /// Chunk load/generation sequence at submit (0 when unavailable).
  uint64_t chunk_incarnation{0};
  WorkDomain domain{WorkDomain::MeshCapture};
  /// Per-domain cancel generation or job sequence.
  uint64_t generation{0};

  bool Matches(const WorkToken &expected) const
  {
    return world_epoch == expected.world_epoch && coord == expected.coord &&
           chunk_incarnation == expected.chunk_incarnation &&
           domain == expected.domain && generation == expected.generation;
  }

  bool SameEpochAndCoord(const WorkToken &expected) const
  {
    return world_epoch == expected.world_epoch && coord == expected.coord &&
           domain == expected.domain;
  }
};

/// Input revisions captured at job submit; reject apply when stale (M09).
struct DependencyStamp
{
  uint64_t content_revision{0};
  uint64_t light_revision{0};
  uint64_t material_catalog_revision{0};
  /// Neighbor halo: ±X, ∓X, ±Z, ∓Z, ±Y, ∓Y light-field revisions (0 if absent).
  std::array<uint64_t, 6> halo_light_revision{};

  bool Matches(const DependencyStamp &current) const
  {
    return content_revision == current.content_revision &&
           light_revision == current.light_revision &&
           material_catalog_revision == current.material_catalog_revision &&
           halo_light_revision == current.halo_light_revision;
  }
};

inline uint64_t MaterialCatalogRevision(
    const std::shared_ptr<const BlockDefinitionCatalog> &catalog)
{
  return reinterpret_cast<uintptr_t>(catalog.get());
}

} // namespace cutum
