#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"

namespace cutum
{

namespace
{

bool InChunkLocal(glm::ivec3 local)
{
  return local.x >= 0 && local.x < CHUNK_SIZE && local.y >= 0 &&
         local.y < CHUNK_SIZE && local.z >= 0 && local.z < CHUNK_SIZE;
}

} // namespace

ChunkMeshSnapshot ChunkMeshSnapshot::Capture(const UBlockWorld &world,
                                             glm::ivec3 chunkCoord,
                                             uint64_t sourceRevision)
{
  ChunkMeshSnapshot snapshot;
  snapshot.coord = chunkCoord;
  snapshot.sourceRevision = sourceRevision;
  const UChunk *chunk = world.GetChunkManager().GetChunk(chunkCoord);
  if (!chunk)
  {
    return snapshot;
  }
  snapshot.blocks = chunk->GetData();
  snapshot.fluid_packed = chunk->GetFluidData();

  const glm::ivec3 origin = snapshot.ChunkOrigin();
  for (int axis = 0; axis < 3; ++axis)
  {
    for (int sign = -1; sign <= 1; sign += 2)
    {
      glm::ivec3 neighborOffset(0);
      neighborOffset[axis] = sign;
      for (int u = 0; u < CHUNK_SIZE; ++u)
      {
        for (int v = 0; v < CHUNK_SIZE; ++v)
        {
          glm::ivec3 local(0);
          const int uAxis = (axis + 1) % 3;
          const int vAxis = (axis + 2) % 3;
          local[axis] = sign < 0 ? -1 : CHUNK_SIZE;
          local[uAxis] = u;
          local[vAxis] = v;
          const glm::ivec3 worldPos = origin + local;
          snapshot.shellBlocks[worldPos] = world.GetBlock(worldPos);
          const uint8_t packed =
              PackFluidCellState(world.GetFluidState(worldPos));
          if (packed != 0)
          {
            snapshot.shellFluid[worldPos] = packed;
          }
        }
      }
    }
  }
  return snapshot;
}

BlockId ChunkMeshSnapshot::GetBlock(glm::ivec3 worldPos) const
{
  const glm::ivec3 local = worldPos - ChunkOrigin();
  if (InChunkLocal(local))
  {
    return GetBlockLocal(local);
  }
  const auto it = shellBlocks.find(worldPos);
  if (it != shellBlocks.end())
  {
    return it->second;
  }
  return BLOCK_AIR;
}

BlockId ChunkMeshSnapshot::GetBlockLocal(glm::ivec3 local) const
{
  return blocks[local.x + CHUNK_SIZE * local.y + CHUNK_SIZE * CHUNK_SIZE * local.z];
}

uint8_t ChunkMeshSnapshot::GetFluidPackedLocal(glm::ivec3 local) const
{
  return fluid_packed[local.x + CHUNK_SIZE * local.y +
                      CHUNK_SIZE * CHUNK_SIZE * local.z];
}

FluidCellState ChunkMeshSnapshot::GetFluidLocal(glm::ivec3 local) const
{
  return UnpackFluidCellState(GetFluidPackedLocal(local));
}

uint8_t ChunkMeshSnapshot::GetFluidPacked(glm::ivec3 worldPos) const
{
  const glm::ivec3 local = worldPos - ChunkOrigin();
  if (InChunkLocal(local))
  {
    return GetFluidPackedLocal(local);
  }
  const auto it = shellFluid.find(worldPos);
  if (it != shellFluid.end())
  {
    return it->second;
  }
  return 0;
}

FluidCellState ChunkMeshSnapshot::GetFluid(glm::ivec3 worldPos) const
{
  return UnpackFluidCellState(GetFluidPacked(worldPos));
}

glm::ivec3 ChunkMeshSnapshot::ChunkOrigin() const
{
  return coord * CHUNK_SIZE;
}

} // namespace cutum
