#pragma once
#include "World/Chunks/Chunk.h"
#include <cstddef>

namespace cutum
{
// Mesh lighting may sample face-adjacent cells plus horizontal fallback cells.
// That read set can touch the center chunk and all 26 chunks in its 3x3x3
// neighborhood, so capture and validate the whole neighborhood explicitly.
inline constexpr std::size_t kChunkMeshInputStampCount = 27;
inline constexpr std::size_t kChunkMeshNeighborStampCount =
    kChunkMeshInputStampCount - 1;
inline constexpr int kChunkMeshLightHaloRadius = 2;

/// Hash the exact light cells a chunk at `neighbor_offset` contributes to a
/// center chunk's radius-2 padded light input. The signature intentionally
/// ignores unrelated light changes in the neighbor's interior.
inline uint64_t ChunkMeshLightHaloSignature(const UChunk *neighbor,
                                           glm::ivec3 neighbor_offset)
{
  constexpr uint64_t kFnvOffset = 1469598103934665603ull;
  constexpr uint64_t kFnvPrime = 1099511628211ull;
  glm::ivec3 source_min(0);
  glm::ivec3 source_max(CHUNK_SIZE - 1);
  for (int axis = 0; axis < 3; ++axis)
  {
    if (neighbor_offset[axis] < 0)
    {
      source_min[axis] = CHUNK_SIZE - kChunkMeshLightHaloRadius;
    }
    else if (neighbor_offset[axis] > 0)
    {
      source_max[axis] = kChunkMeshLightHaloRadius - 1;
    }
  }
  uint64_t hash = kFnvOffset;
  for (int y = source_min.y; y <= source_max.y; ++y)
  {
    for (int z = source_min.z; z <= source_max.z; ++z)
    {
      for (int x = source_min.x; x <= source_max.x; ++x)
      {
        const uint8_t packed = neighbor
                                   ? neighbor->GetLightPackedLocal(
                                         glm::ivec3(x, y, z))
                                   : 0;
        hash ^= packed;
        hash *= kFnvPrime;
      }
    }
  }
  return hash;
}

// Captured under the same ownership as voxel/light reads. Geometry readers
// use incarnation/content identity; the mesh's neighbor-light halo is tracked
// separately by exact sampled-cell signatures.
// Strategy A Phase1–Q4a: MeshGeomStamp = incarnation/content/light only.
// Neighbor visual residency may affect shell Capture for occlusion preview but
// MUST NOT enter stamp equality / InputsStillValid (SoftDefer 0↔1 thrash).
struct ChunkInputStamp
{
  glm::ivec3 coord{0};
  uint64_t incarnation{0};
  uint64_t content{0};
  uint64_t light{0};
  bool readsLight{true};
  bool readsContent{true};
  static ChunkInputStamp Capture(glm::ivec3 coord, const UChunk *chunk,
                                 bool reads_light = true,
                                 bool reads_content = true)
  {
    return {coord, chunk ? chunk->GetIncarnation() : 0,
            chunk ? chunk->GetContentRevision() : 0,
            chunk ? chunk->GetLightFieldRevision() : 0, reads_light,
            reads_content};
  }
  bool Matches(const UChunk *chunk) const
  {
    if (!readsContent && !readsLight)
    {
      return true;
    }
    const auto current = Capture(coord, chunk, readsLight, readsContent);
    return (!(readsContent || readsLight) ||
            incarnation == current.incarnation) &&
           (!readsContent || content == current.content) &&
           (!readsLight || light == current.light);
  }
};
} // namespace cutum
