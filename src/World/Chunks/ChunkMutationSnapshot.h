#pragma once

#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include <vector>

namespace cutum
{

/// Capture content identities around a bounded operation without copying voxel
/// arrays. Includes absent coordinates so creation/removal/reuse is observable.
/// Light-only writes do not count as geometry/fluid mutations.
class ChunkMutationSnapshot
{
public:
  ChunkMutationSnapshot(const UBlockWorld &world, glm::ivec3 low,
                        glm::ivec3 high)
  {
    for (int64_t y = low.y; y <= high.y; ++y)
      for (int64_t z = low.z; z <= high.z; ++z)
        for (int64_t x = low.x; x <= high.x; ++x)
        {
          const glm::ivec3 coord(static_cast<int>(x), static_cast<int>(y),
                                 static_cast<int>(z));
          const auto *chunk = world.GetChunkManager().GetChunk(coord);
          Records.push_back({coord, chunk != nullptr,
                             chunk ? chunk->GetIncarnation() : 0,
                             chunk ? chunk->GetContentRevision() : 0});
        }
  }

  void AppendChanged(const UBlockWorld &world,
                     std::vector<glm::ivec3> &out) const
  {
    for (const auto &before : Records)
    {
      const auto *after = world.GetChunkManager().GetChunk(before.coord);
      if ((after != nullptr) != before.present ||
          (after && (after->GetIncarnation() != before.incarnation ||
                     after->GetContentRevision() != before.content_revision)))
        out.push_back(before.coord);
    }
  }

private:
  struct Record
  {
    glm::ivec3 coord;
    bool present;
    uint64_t incarnation;
    uint64_t content_revision;
  };
  std::vector<Record> Records;
};

} // namespace cutum
