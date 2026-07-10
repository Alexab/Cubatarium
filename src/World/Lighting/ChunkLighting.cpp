#include "World/Lighting/ChunkLighting.h"

#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/LightUtil.h"
#include "World/Math/GridMath.h"

#include <algorithm>
#include <array>
#include <deque>
#include <unordered_set>
#include <vector>

namespace cutum
{

namespace
{

constexpr int kSkyScanAboveBlocks = 15 * CHUNK_SIZE;

bool InChunkLocal(glm::ivec3 local)
{
  return local.x >= 0 && local.x < CHUNK_SIZE && local.y >= 0 &&
         local.y < CHUNK_SIZE && local.z >= 0 && local.z < CHUNK_SIZE;
}

bool WorldPosInChunk(glm::ivec3 world_pos, glm::ivec3 chunk_coord)
{
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  const glm::ivec3 local = world_pos - origin;
  return InChunkLocal(local);
}

void ClearChunkLight(UChunk &chunk)
{
  chunk.GetLightDataMutable().fill(0);
}

void WriteSkyLight(UChunk &chunk, glm::ivec3 local, int sky_level)
{
  const int block_level = chunk.GetBlockLightLocal(local);
  chunk.SetLightLocal(local, sky_level, block_level);
}

void WriteBlockLight(UChunk &chunk, glm::ivec3 local, int block_level)
{
  const int sky_level = chunk.GetSkyLightLocal(local);
  chunk.SetLightLocal(local, sky_level, block_level);
}

int GetSkyLightWorld(const UBlockWorld &world, glm::ivec3 world_pos)
{
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(world_pos);
  const UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return 0;
  }
  return chunk->GetSkyLightLocal(UChunkManager::WorldToLocal(world_pos));
}

int GetBlockLightWorld(const UBlockWorld &world, glm::ivec3 world_pos)
{
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(world_pos);
  const UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return 0;
  }
  return chunk->GetBlockLightLocal(UChunkManager::WorldToLocal(world_pos));
}

int SkylightStepCost(const UBlockRegistry &registry, BlockId id)
{
  if (!IsLightTransparent(registry, id))
  {
    return kMaxLightLevel + 1;
  }
  if (registry.IsLiquid(id))
  {
    return 2;
  }
  return 1;
}

int VerticalSkylightStepCost(const UBlockRegistry &registry, BlockId id)
{
  if (!IsLightTransparent(registry, id))
  {
    return kMaxLightLevel + 1;
  }
  // Direct sky column should stay bright through air/cross/cutout blocks.
  if (registry.IsLiquid(id))
  {
    return 1;
  }
  return 0;
}

void PropagateSkylightColumn(UBlockWorld &world, UBlockRegistry &registry,
                             UChunk &chunk, glm::ivec3 chunk_coord, int local_x,
                             int local_z)
{
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  const int world_x = origin.x + local_x;
  const int world_z = origin.z + local_z;
  const int top_y = origin.y + CHUNK_SIZE - 1;
  int incoming = kMaxLightLevel;
  for (int y = top_y + 1; y < top_y + kSkyScanAboveBlocks; ++y)
  {
    const BlockId id = world.GetBlock(glm::ivec3(world_x, y, world_z));
    if (!IsLightTransparent(registry, id))
    {
      incoming = 0;
      break;
    }
  }
  for (int y = top_y; y >= origin.y; --y)
  {
    const glm::ivec3 world_pos(world_x, y, world_z);
    const BlockId id = world.GetBlock(world_pos);
    const glm::ivec3 local(local_x, y - origin.y, local_z);
    if (IsLightTransparent(registry, id))
    {
      WriteSkyLight(chunk, local, incoming);
    }
    incoming = std::max(0, incoming - VerticalSkylightStepCost(registry, id));
  }
}

void PropagateSkylightHorizontal(UBlockWorld &world, UBlockRegistry &registry,
                                 UChunk &chunk, glm::ivec3 chunk_coord)
{
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  std::deque<glm::ivec3> queue;
  for (int ly = 0; ly < CHUNK_SIZE; ++ly)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        const glm::ivec3 local(lx, ly, lz);
        const BlockId id = chunk.GetBlockLocal(local);
        if (IsLightTransparent(registry, id) &&
            chunk.GetSkyLightLocal(local) > 0)
        {
          queue.push_back(origin + local);
        }
      }
    }
  }

  while (!queue.empty())
  {
    const glm::ivec3 pos = queue.front();
    queue.pop_front();
    const int light = GetSkyLightWorld(world, pos);
    if (light <= 1)
    {
      continue;
    }
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      const glm::ivec3 neighbor = pos + offset;
      if (!WorldPosInChunk(neighbor, chunk_coord))
      {
        continue;
      }
      const BlockId neighbor_id = world.GetBlock(neighbor);
      if (!IsLightTransparent(registry, neighbor_id))
      {
        continue;
      }
      const int next =
          std::max(0, light - SkylightStepCost(registry, neighbor_id));
      if (next <= GetSkyLightWorld(world, neighbor))
      {
        continue;
      }
      const glm::ivec3 local = neighbor - origin;
      WriteSkyLight(chunk, local, next);
      queue.push_back(neighbor);
    }
  }
}

void PropagateBlocklight(UBlockWorld &world, UBlockRegistry &registry,
                         UChunk &chunk, glm::ivec3 chunk_coord)
{
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  std::deque<std::pair<glm::ivec3, int>> queue;

  const auto seed_chunk = [&](glm::ivec3 neighbor_coord)
  {
    const UChunk *neighbor = world.GetChunkManager().GetChunk(neighbor_coord);
    if (!neighbor)
    {
      return;
    }
    const glm::ivec3 neighbor_origin = neighbor_coord * CHUNK_SIZE;
    for (int ly = 0; ly < CHUNK_SIZE; ++ly)
    {
      for (int lz = 0; lz < CHUNK_SIZE; ++lz)
      {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx)
        {
          const glm::ivec3 local(lx, ly, lz);
          const BlockId id = neighbor->GetBlockLocal(local);
          const int emission = BlockEmissionLevel(registry, id);
          if (emission > 0)
          {
            queue.emplace_back(neighbor_origin + local, emission);
          }
        }
      }
    }
  };

  seed_chunk(chunk_coord);
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    seed_chunk(chunk_coord + offset);
  }

  while (!queue.empty())
  {
    const auto [pos, light] = queue.front();
    queue.pop_front();
    if (WorldPosInChunk(pos, chunk_coord))
    {
      if (light <= GetBlockLightWorld(world, pos))
      {
        continue;
      }
      const glm::ivec3 local = pos - origin;
      WriteBlockLight(chunk, local, light);
    }
    if (light <= 1)
    {
      continue;
    }
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      const glm::ivec3 neighbor = pos + offset;
      if (!WorldPosInChunk(neighbor, chunk_coord))
      {
        continue;
      }
      const BlockId neighbor_id = world.GetBlock(neighbor);
      if (!IsLightTransparent(registry, neighbor_id))
      {
        continue;
      }
      const int next = light - 1;
      if (next > GetBlockLightWorld(world, neighbor))
      {
        queue.emplace_back(neighbor, next);
      }
    }
  }
}

} // namespace

void RelightChunk(UBlockWorld &world, UBlockRegistry &registry,
                  glm::ivec3 chunk_coord, bool include_block_light)
{
  UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return;
  }

  ClearChunkLight(*chunk);
  for (int lx = 0; lx < CHUNK_SIZE; ++lx)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      PropagateSkylightColumn(world, registry, *chunk, chunk_coord, lx, lz);
    }
  }
  PropagateSkylightHorizontal(world, registry, *chunk, chunk_coord);
  if (include_block_light)
  {
    PropagateBlocklight(world, registry, *chunk, chunk_coord);
  }
}

void RelightChunksAround(UBlockWorld &world, UBlockRegistry &registry,
                         glm::ivec3 block_pos)
{
  const glm::ivec3 center = UChunkManager::WorldToChunk(block_pos);
  std::vector<glm::ivec3> coords;
  coords.push_back(center);
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    const glm::ivec3 neighbor = center + offset;
    if (world.GetChunkManager().HasChunk(neighbor))
    {
      coords.push_back(neighbor);
    }
  }
  for (const glm::ivec3 &coord : coords)
  {
    RelightChunk(world, registry, coord);
  }
}

void RelightColumn(UBlockWorld &world, UBlockRegistry &registry, int world_x,
                   int world_z, int min_y, int max_y, bool include_block_light)
{
  const glm::ivec3 base =
      UChunkManager::WorldToChunk(glm::ivec3(world_x, min_y, world_z));
  const glm::ivec3 top =
      UChunkManager::WorldToChunk(glm::ivec3(world_x, max_y, world_z));
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int cy = base.y; cy <= top.y; ++cy)
      {
        coords.insert(glm::ivec3(base.x + dx, cy, base.z + dz));
      }
    }
  }
  for (const glm::ivec3 &coord : coords)
  {
    if (world.GetChunkManager().HasChunk(coord))
    {
      RelightChunk(world, registry, coord, include_block_light);
    }
  }
}

void RelightAllLoadedChunks(UBlockWorld &world, UBlockRegistry &registry)
{
  world.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      { RelightChunk(world, registry, chunk.GetCoord()); });
}

} // namespace cutum
