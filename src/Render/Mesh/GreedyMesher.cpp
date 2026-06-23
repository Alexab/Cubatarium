#include "Render/Mesh/GreedyMesher.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include <algorithm>
#include <cstring>

namespace cutum
{

namespace
{

bool NeighborHidesFace(const UBlockWorld &world, UBlockRegistry &registry,
                       BlockId faceId, glm::ivec3 blockPos,
                       glm::ivec3 neighborOffset)
{
  const glm::ivec3 neighborPos = blockPos + neighborOffset;
  const BlockId neighbor = world.GetBlock(neighborPos);
  if (neighbor == BLOCK_AIR)
  {
    return false;
  }

  const BlockRenderStyle faceStyle = registry.GetRenderStyle(faceId);
  if (faceStyle == BlockRenderStyle::Cutout)
  {
    if (registry.GetRenderStyle(neighbor) == BlockRenderStyle::Cutout)
    {
      return false;
    }
  }

  // Hide shared faces between identical fluid blocks (avoids hollow water/fire
  // volumes).
  if (neighbor == faceId && !registry.BlocksMovement(faceId))
  {
    return true;
  }

  const bool faceTransparent = registry.IsTransparent(faceId);
  const bool neighborTransparent = registry.IsTransparent(neighbor);

  if (faceTransparent && neighborTransparent)
  {
    return true;
  }

  const BlockRenderStyle neighborRenderStyle =
      registry.GetRenderStyle(neighbor);

  // Fluid↔opaque: solid draws the shared face (sand/stone texture); fluid side
  // is culled so water does not paint a translucent skin on cliff blocks.
  if (faceStyle == BlockRenderStyle::Fluid && !neighborTransparent)
  {
    return true;
  }
  if (!faceTransparent && neighborRenderStyle == BlockRenderStyle::Fluid)
  {
    return false;
  }

  // Two-hop for opaque ↔ solid transparent cubes (glass, ice), not fluids.
  if (!faceTransparent && neighborTransparent &&
      registry.BlocksMovement(neighbor))
  {
    const glm::ivec3 beyondPos = neighborPos + neighborOffset;
    const glm::ivec3 beforePos = blockPos - neighborOffset;
    const BlockId beyond = world.GetBlock(beyondPos);
    const BlockId before = world.GetBlock(beforePos);
    const bool beyondOpen =
        (beyond == BLOCK_AIR || !registry.BlocksMovement(beyond));
    const bool beforeSolid =
        (before != BLOCK_AIR && registry.BlocksMovement(before));
    if (beyondOpen && beforeSolid)
    {
      return false;
    }
    if (beyondOpen)
    {
      return true;
    }
    return false;
  }

  if (registry.BlocksMovement(neighbor))
  {
    return true;
  }
  return false;
}

bool NeighborHidesFaceSnapshot(const ChunkMeshSnapshot &snapshot,
                               UBlockRegistry &registry, BlockId faceId,
                               glm::ivec3 blockPos, glm::ivec3 neighborOffset)
{
  const glm::ivec3 neighborPos = blockPos + neighborOffset;
  const BlockId neighbor = snapshot.GetBlock(neighborPos);
  if (neighbor == BLOCK_AIR)
  {
    return false;
  }

  const BlockRenderStyle faceStyle = registry.GetRenderStyle(faceId);
  if (faceStyle == BlockRenderStyle::Cutout)
  {
    if (registry.GetRenderStyle(neighbor) == BlockRenderStyle::Cutout)
    {
      return false;
    }
  }

  if (neighbor == faceId && !registry.BlocksMovement(faceId))
  {
    return true;
  }

  const bool faceTransparent = registry.IsTransparent(faceId);
  const bool neighborTransparent = registry.IsTransparent(neighbor);

  if (faceTransparent && neighborTransparent)
  {
    return true;
  }

  const BlockRenderStyle neighborRenderStyle =
      registry.GetRenderStyle(neighbor);

  if (faceStyle == BlockRenderStyle::Fluid && !neighborTransparent)
  {
    return true;
  }
  if (!faceTransparent && neighborRenderStyle == BlockRenderStyle::Fluid)
  {
    return false;
  }

  if (!faceTransparent && neighborTransparent &&
      registry.BlocksMovement(neighbor))
  {
    const glm::ivec3 beyondPos = neighborPos + neighborOffset;
    const glm::ivec3 beforePos = blockPos - neighborOffset;
    const BlockId beyond = snapshot.GetBlock(beyondPos);
    const BlockId before = snapshot.GetBlock(beforePos);
    const bool beyondOpen =
        (beyond == BLOCK_AIR || !registry.BlocksMovement(beyond));
    const bool beforeSolid =
        (before != BLOCK_AIR && registry.BlocksMovement(before));
    if (beyondOpen && beforeSolid)
    {
      return false;
    }
    if (beyondOpen)
    {
      return true;
    }
    return false;
  }

  if (registry.BlocksMovement(neighbor))
  {
    return true;
  }
  return false;
}

int MaxSolidLocalY(const ChunkMeshSnapshot &snapshot, UBlockRegistry &registry)
{
  int maxY = -1;
  for (int z = 0; z < CHUNK_SIZE; ++z)
  {
    for (int y = 0; y < CHUNK_SIZE; ++y)
    {
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const BlockId id = snapshot.GetBlockLocal(glm::ivec3(x, y, z));
        if (id != BLOCK_AIR && registry.GetRenderStyle(id) != BlockRenderStyle::Cross)
        {
          maxY = std::max(maxY, y);
        }
      }
    }
  }
  return maxY;
}

} // namespace

std::vector<GreedyQuad> UGreedyMesher::BuildChunkMesh(const UBlockWorld &world,
                                                      glm::ivec3 chunkCoord,
                                                      UBlockRegistry &registry)
{
  std::vector<GreedyQuad> quads;
  const UChunk *chunk = world.GetChunkManager().GetChunk(chunkCoord);
  if (!chunk)
  {
    return quads;
  }

  int maxSolidY = -1;
  for (int z = 0; z < CHUNK_SIZE; ++z)
  {
    for (int y = 0; y < CHUNK_SIZE; ++y)
    {
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const BlockId id = chunk->GetBlockLocal(glm::ivec3(x, y, z));
        if (id != BLOCK_AIR &&
            registry.GetRenderStyle(id) != BlockRenderStyle::Cross)
        {
          maxSolidY = std::max(maxSolidY, y);
        }
      }
    }
  }

  BlockId mask[CHUNK_SIZE][CHUNK_SIZE];

  for (int axis = 0; axis < 3; ++axis)
  {
    const int uAxis = (axis + 1) % 3;
    const int vAxis = (axis + 2) % 3;

    for (int sign = -1; sign <= 1; sign += 2)
    {
      for (int slice = 0; slice < CHUNK_SIZE; ++slice)
      {
        if (axis == 1 && maxSolidY >= 0 && slice > maxSolidY)
        {
          continue;
        }
        std::memset(mask, 0, sizeof(mask));

        for (int v = 0; v < CHUNK_SIZE; ++v)
        {
          for (int u = 0; u < CHUNK_SIZE; ++u)
          {
            glm::ivec3 local(0);
            local[axis] = slice;
            local[uAxis] = u;
            local[vAxis] = v;

            const glm::ivec3 worldPos(chunkCoord.x * CHUNK_SIZE + local.x,
                                      chunkCoord.y * CHUNK_SIZE + local.y,
                                      chunkCoord.z * CHUNK_SIZE + local.z);

            const BlockId Id = chunk->GetBlockLocal(local);
            if (Id == BLOCK_AIR)
            {
              continue;
            }
            if (registry.GetRenderStyle(Id) == BlockRenderStyle::Cross)
            {
              continue;
            }

            glm::ivec3 neighborOffset(0);
            neighborOffset[axis] = sign;
            glm::ivec3 neighborPos = worldPos + neighborOffset;
            if (NeighborHidesFace(world, registry, Id, worldPos, neighborOffset))
            {
              continue;
            }

            mask[v][u] = Id;
          }
        }

        for (int v = 0; v < CHUNK_SIZE; ++v)
        {
          for (int u = 0; u < CHUNK_SIZE; ++u)
          {
            const BlockId Id = mask[v][u];
            if (Id == BLOCK_AIR)
            {
              continue;
            }

            int width = 1;
            while (u + width < CHUNK_SIZE && mask[v][u + width] == Id)
            {
              ++width;
            }

            int height = 1;
            bool done = false;
            while (v + height < CHUNK_SIZE && !done)
            {
              for (int k = 0; k < width; ++k)
              {
                if (mask[v + height][u + k] != Id)
                {
                  done = true;
                  break;
                }
              }
              if (!done)
              {
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
            quad.Id = Id;
            quad.faceSign = sign;
            quads.push_back(quad);

            for (int dv = 0; dv < height; ++dv)
            {
              for (int du = 0; du < width; ++du)
              {
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

std::vector<GreedyQuad> UGreedyMesher::BuildChunkMesh(
    const ChunkMeshSnapshot &snapshot, UBlockRegistry &registry)
{
  std::vector<GreedyQuad> quads;
  const glm::ivec3 chunkCoord = snapshot.coord;
  const int maxSolidY = MaxSolidLocalY(snapshot, registry);

  BlockId mask[CHUNK_SIZE][CHUNK_SIZE];

  for (int axis = 0; axis < 3; ++axis)
  {
    const int uAxis = (axis + 1) % 3;
    const int vAxis = (axis + 2) % 3;

    for (int sign = -1; sign <= 1; sign += 2)
    {
      for (int slice = 0; slice < CHUNK_SIZE; ++slice)
      {
        if (axis == 1 && maxSolidY >= 0 && slice > maxSolidY)
        {
          continue;
        }
        std::memset(mask, 0, sizeof(mask));

        for (int v = 0; v < CHUNK_SIZE; ++v)
        {
          for (int u = 0; u < CHUNK_SIZE; ++u)
          {
            glm::ivec3 local(0);
            local[axis] = slice;
            local[uAxis] = u;
            local[vAxis] = v;

            const glm::ivec3 worldPos(chunkCoord.x * CHUNK_SIZE + local.x,
                                      chunkCoord.y * CHUNK_SIZE + local.y,
                                      chunkCoord.z * CHUNK_SIZE + local.z);

            const BlockId Id = snapshot.GetBlockLocal(local);
            if (Id == BLOCK_AIR)
            {
              continue;
            }
            if (registry.GetRenderStyle(Id) == BlockRenderStyle::Cross)
            {
              continue;
            }

            glm::ivec3 neighborOffset(0);
            neighborOffset[axis] = sign;
            if (NeighborHidesFaceSnapshot(snapshot, registry, Id, worldPos,
                                          neighborOffset))
            {
              continue;
            }

            mask[v][u] = Id;
          }
        }

        for (int v = 0; v < CHUNK_SIZE; ++v)
        {
          for (int u = 0; u < CHUNK_SIZE; ++u)
          {
            const BlockId Id = mask[v][u];
            if (Id == BLOCK_AIR)
            {
              continue;
            }

            int width = 1;
            while (u + width < CHUNK_SIZE && mask[v][u + width] == Id)
            {
              ++width;
            }

            int height = 1;
            bool done = false;
            while (v + height < CHUNK_SIZE && !done)
            {
              for (int k = 0; k < width; ++k)
              {
                if (mask[v + height][u + k] != Id)
                {
                  done = true;
                  break;
                }
              }
              if (!done)
              {
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
            quad.Id = Id;
            quad.faceSign = sign;
            quads.push_back(quad);

            for (int dv = 0; dv < height; ++dv)
            {
              for (int du = 0; du < width; ++du)
              {
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

} // namespace cutum
