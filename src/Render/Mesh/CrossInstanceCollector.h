#ifndef CROSSINSTANCECOLLECTOR_H
#define CROSSINSTANCECOLLECTOR_H

#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/CrossInstanceBatch.h"
#include "Render/Mesh/MeshLightSampling.h"
#include "World/Chunks/Chunk.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/GridMath.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace cutum
{

inline void CollectCrossInstancesFromChunk(
    const UChunk &chunk, glm::ivec3 chunk_coord,
    const UBlockRegistry &registry,
    std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> &out)
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
        const CrossInstanceLight light =
            SampleCrossInstanceLight(chunk, world_pos);
        CrossInstanceGpu instance;
        instance.center = BlockCenter(world_pos);
        instance.skyLight = light.skyLight;
        instance.blockLight = light.blockLight;
        out[id].push_back(instance);
      }
    }
  }
}

inline void CollectCrossInstancesFromSnapshot(
    const ChunkMeshSnapshot &snapshot, const UBlockRegistry &registry,
    std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> &out)
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
        const CrossInstanceLight light =
            SampleCrossInstanceLight(snapshot, world_pos);
        CrossInstanceGpu instance;
        instance.center = BlockCenter(world_pos);
        instance.skyLight = light.skyLight;
        instance.blockLight = light.blockLight;
        out[id].push_back(instance);
      }
    }
  }
}

} // namespace cutum

#endif
