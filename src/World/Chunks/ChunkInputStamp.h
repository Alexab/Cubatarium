#pragma once
#include "World/Chunks/Chunk.h"

namespace cutum
{
// Captured under the same ownership as voxel/light reads. Absence is explicit
// (incarnation zero), so unload/reload and newly resident neighbors invalidate.
struct ChunkInputStamp
{
  glm::ivec3 coord{0};
  uint64_t incarnation{0};
  uint64_t content{0};
  uint64_t light{0};
  bool readsLight{true};
  static ChunkInputStamp Capture(glm::ivec3 coord, const UChunk *chunk,
                                 bool reads_light = true)
  {
    return {coord, chunk ? chunk->GetIncarnation() : 0,
            chunk ? chunk->GetContentRevision() : 0,
            chunk ? chunk->GetLightFieldRevision() : 0, reads_light};
  }
  bool Matches(const UChunk *chunk) const
  {
    const auto current = Capture(coord, chunk, readsLight);
    return incarnation == current.incarnation && content == current.content &&
           (!readsLight || light == current.light);
  }
};
} // namespace cutum
