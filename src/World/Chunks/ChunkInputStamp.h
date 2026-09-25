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

// Captured under the same ownership as voxel/light reads. Absence is explicit
// (incarnation zero), so unload/reload and newly resident neighbors invalidate.
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
    const auto current = Capture(coord, chunk, readsLight);
    return incarnation == current.incarnation &&
           (!readsContent || content == current.content) &&
           (!readsLight || light == current.light);
  }
};
} // namespace cutum
