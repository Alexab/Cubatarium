#ifndef CROSSINSTANCECOLLECTOR_H
#define CROSSINSTANCECOLLECTOR_H

#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "World/Chunks/Chunk.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/GridMath.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace cutum
{

inline void CollectCrossCentersFromChunk(
    const UChunk &chunk, glm::ivec3 chunk_coord,
    const UBlockRegistry &registry,
    std::unordered_map<BlockId, std::vector<glm::vec3>> &out)
{
  for (int ly = 0; ly < CHUNK_SIZE; ++ly)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        const glm::ivec3 local(lx, ly, lz);
        const BlockId id = chunk.GetBlockLocal(local);
        if (id == BLOCK_AIR ||
            registry.GetRenderStyle(id) != BlockRenderStyle::Cross)
        {
          continue;
        }
        const glm::ivec3 world_pos(chunk_coord.x * CHUNK_SIZE + lx,
                                   chunk_coord.y * CHUNK_SIZE + ly,
                                   chunk_coord.z * CHUNK_SIZE + lz);
        out[id].push_back(BlockVisualCenter(world_pos));
      }
    }
  }
}

inline void CollectCrossCentersFromSnapshot(
    const ChunkMeshSnapshot &snapshot, const UBlockRegistry &registry,
    std::unordered_map<BlockId, std::vector<glm::vec3>> &out)
{
  for (int ly = 0; ly < CHUNK_SIZE; ++ly)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        const glm::ivec3 local(lx, ly, lz);
        const BlockId id = snapshot.GetBlockLocal(local);
        if (id == BLOCK_AIR ||
            registry.GetRenderStyle(id) != BlockRenderStyle::Cross)
        {
          continue;
        }
        const glm::ivec3 world_pos(snapshot.coord.x * CHUNK_SIZE + lx,
                                   snapshot.coord.y * CHUNK_SIZE + ly,
                                   snapshot.coord.z * CHUNK_SIZE + lz);
        out[id].push_back(BlockVisualCenter(world_pos));
      }
    }
  }
}

} // namespace cutum

#endif
