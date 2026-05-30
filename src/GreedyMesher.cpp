#include "GreedyMesher.h"
#include "BlockRegistry.h"
#include "BlockWorld.h"
#include "ChunkManager.h"
#include "Chunk.h"
#include <cstring>

namespace cutum {

namespace {

bool IsSolid(const BlockWorld& world, BlockRegistry& registry, glm::ivec3 pos)
{
 return registry.IsSolid(world.GetBlock(pos));
}

} // namespace

std::vector<GreedyQuad> GreedyMesher::BuildChunkMesh(
    const BlockWorld& world, glm::ivec3 chunkCoord, BlockRegistry& registry)
{
 std::vector<GreedyQuad> quads;
 const Chunk* chunk = world.GetChunkManager().GetChunk(chunkCoord);
 if (!chunk) {
  return quads;
 }

 BlockId mask[CHUNK_SIZE][CHUNK_SIZE];

 for (int axis = 0; axis < 3; ++axis) {
  const int uAxis = (axis + 1) % 3;
  const int vAxis = (axis + 2) % 3;

  for (int sign = -1; sign <= 1; sign += 2) {
   for (int slice = 0; slice < CHUNK_SIZE; ++slice) {
    std::memset(mask, 0, sizeof(mask));

    for (int v = 0; v < CHUNK_SIZE; ++v) {
     for (int u = 0; u < CHUNK_SIZE; ++u) {
      glm::ivec3 local(0);
      local[axis] = slice;
      local[uAxis] = u;
      local[vAxis] = v;

      const glm::ivec3 worldPos(
          chunkCoord.x * CHUNK_SIZE + local.x,
          chunkCoord.y * CHUNK_SIZE + local.y,
          chunkCoord.z * CHUNK_SIZE + local.z);

      const BlockId id = chunk->GetBlockLocal(local);
      if (!registry.IsSolid(id)) {
       continue;
      }

      glm::ivec3 neighborPos = worldPos;
      neighborPos[axis] += sign;
      if (IsSolid(world, registry, neighborPos)) {
       continue;
      }

      mask[v][u] = id;
     }
    }

    for (int v = 0; v < CHUNK_SIZE; ++v) {
     for (int u = 0; u < CHUNK_SIZE; ++u) {
      const BlockId id = mask[v][u];
      if (id == BLOCK_AIR) {
       continue;
      }

      int width = 1;
      while (u + width < CHUNK_SIZE && mask[v][u + width] == id) {
       ++width;
      }

      int height = 1;
      bool done = false;
      while (v + height < CHUNK_SIZE && !done) {
       for (int k = 0; k < width; ++k) {
        if (mask[v + height][u + k] != id) {
         done = true;
         break;
        }
       }
       if (!done) {
        ++height;
       }
      }

      GreedyQuad quad;
      quad.axis = axis;
      quad.slice = slice;
      quad.u = u;
      quad.v = v;
      quad.width = width;
      quad.height = height;
      quad.id = id;
      quad.faceSign = sign;
      quads.push_back(quad);

      for (int dv = 0; dv < height; ++dv) {
       for (int du = 0; du < width; ++du) {
        mask[v + dv][u + du] = BLOCK_AIR;
       }
      }
     }
    }
   }
  }
 }

 return quads;
}

}
