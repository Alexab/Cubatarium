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

void ClearChunkSkylight(UChunk &chunk)
{
  for (int ly = 0; ly < CHUNK_SIZE; ++ly)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        WriteSkyLight(chunk, glm::ivec3(lx, ly, lz), 0);
      }
    }
  }
}

void ClearChunkBlockLight(UChunk &chunk)
{
  for (int ly = 0; ly < CHUNK_SIZE; ++ly)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        WriteBlockLight(chunk, glm::ivec3(lx, ly, lz), 0);
      }
    }
  }
}

void WriteSkyLightWorld(UBlockWorld &world, glm::ivec3 world_pos, int sky_level)
{
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(world_pos);
  UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return;
  }
  WriteSkyLight(*chunk, UChunkManager::WorldToLocal(world_pos), sky_level);
}

void WriteBlockLightWorld(UBlockWorld &world, glm::ivec3 world_pos,
                          int block_level)
{
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(world_pos);
  UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return;
  }
  WriteBlockLight(*chunk, UChunkManager::WorldToLocal(world_pos), block_level);
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

bool SharesChunkFace(glm::ivec3 world_pos, glm::ivec3 chunk_coord)
{
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  const glm::ivec3 local = world_pos - origin;
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return false;
  }
  return local.x == 0 || local.x == CHUNK_SIZE - 1 || local.y == 0 ||
         local.y == CHUNK_SIZE - 1 || local.z == 0 || local.z == CHUNK_SIZE - 1;
}

void SeedSkylightHorizontalQueue(UBlockWorld &world, UBlockRegistry &registry,
                                 glm::ivec3 chunk_coord,
                                 std::deque<glm::ivec3> &queue)
{
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  const auto try_seed = [&](glm::ivec3 world_pos, int sky_level)
  {
    if (sky_level <= 0)
    {
      return;
    }
    if (!IsLightTransparent(registry, world.GetBlock(world_pos)))
    {
      return;
    }
    queue.push_back(world_pos);
  };

  if (const UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord))
  {
    for (int ly = 0; ly < CHUNK_SIZE; ++ly)
    {
      for (int lz = 0; lz < CHUNK_SIZE; ++lz)
      {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx)
        {
          const glm::ivec3 local(lx, ly, lz);
          try_seed(origin + local, chunk->GetSkyLightLocal(local));
        }
      }
    }
  }

  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    const glm::ivec3 neighbor_coord = chunk_coord + offset;
    const UChunk *neighbor = world.GetChunkManager().GetChunk(neighbor_coord);
    if (!neighbor)
    {
      continue;
    }
    const glm::ivec3 neighbor_origin = neighbor_coord * CHUNK_SIZE;
    for (int ly = 0; ly < CHUNK_SIZE; ++ly)
    {
      for (int lz = 0; lz < CHUNK_SIZE; ++lz)
      {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx)
        {
          const glm::ivec3 local(lx, ly, lz);
          const glm::ivec3 world_pos = neighbor_origin + local;
          if (!SharesChunkFace(world_pos, chunk_coord))
          {
            continue;
          }
          try_seed(world_pos, neighbor->GetSkyLightLocal(local));
        }
      }
    }
  }
}

void PropagateSkylightHorizontal(UBlockWorld &world, UBlockRegistry &registry,
                                 glm::ivec3 chunk_coord)
{
  std::deque<glm::ivec3> queue;
  SeedSkylightHorizontalQueue(world, registry, chunk_coord, queue);

  while (!queue.empty())
  {
    const glm::ivec3 pos = queue.front();
    queue.pop_front();
    if (!world.GetChunkManager().GetChunk(UChunkManager::WorldToChunk(pos)))
    {
      continue;
    }
    const int light = GetSkyLightWorld(world, pos);
    if (light <= 1)
    {
      continue;
    }
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      const glm::ivec3 neighbor = pos + offset;
      // Unloaded chunk is AIR and Write* no-ops → unbounded BFS / hang.
      if (!world.GetChunkManager().GetChunk(
              UChunkManager::WorldToChunk(neighbor)))
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
      WriteSkyLightWorld(world, neighbor, next);
      queue.push_back(neighbor);
    }
  }
}

void PropagateBlocklight(UBlockWorld &world, UBlockRegistry &registry,
                         glm::ivec3 chunk_coord)
{
  std::deque<std::pair<glm::ivec3, int>> queue;

  const auto seed_chunk = [&](glm::ivec3 seed_coord)
  {
    const UChunk *seed_chunk_ptr = world.GetChunkManager().GetChunk(seed_coord);
    if (!seed_chunk_ptr)
    {
      return;
    }
    const glm::ivec3 seed_origin = seed_coord * CHUNK_SIZE;
    for (int ly = 0; ly < CHUNK_SIZE; ++ly)
    {
      for (int lz = 0; lz < CHUNK_SIZE; ++lz)
      {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx)
        {
          const glm::ivec3 local(lx, ly, lz);
          const glm::ivec3 world_pos = seed_origin + local;
          const BlockId id = seed_chunk_ptr->GetBlockLocal(local);
          const int emission = registry.GetLightEmission(id);
          if (emission > 0)
          {
            queue.emplace_back(world_pos, emission);
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
    if (!world.GetChunkManager().GetChunk(UChunkManager::WorldToChunk(pos)))
    {
      continue;
    }
    if (light <= GetBlockLightWorld(world, pos))
    {
      continue;
    }
    WriteBlockLightWorld(world, pos, light);
    if (light <= 1)
    {
      continue;
    }
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      const glm::ivec3 neighbor = pos + offset;
      if (!world.GetChunkManager().GetChunk(
              UChunkManager::WorldToChunk(neighbor)))
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

constexpr int kEditLightRadius = 15;

bool ChunkLoadedAt(const UBlockWorld &world, glm::ivec3 world_pos)
{
  return world.GetChunkManager().GetChunk(
             UChunkManager::WorldToChunk(world_pos)) != nullptr;
}

void RemoveThenPropagateBlockLight(UBlockWorld &world, UBlockRegistry &registry,
                                   glm::ivec3 origin)
{
  std::deque<std::pair<glm::ivec3, int>> remove_q;
  std::deque<glm::ivec3> add_q;
  std::unordered_set<glm::ivec3, IVec3Hash> add_seen;

  const auto enqueue_add = [&](glm::ivec3 p)
  {
    if (add_seen.insert(p).second)
    {
      add_q.push_back(p);
    }
  };

  // Classic remove from the edited cell only. Zeroing neighbors first wiped
  // nearby torches and required a 31³ emitter rescan (night dig blackness +
  // place-light dead until a second edit; manual 224642).
  const int old_at_origin = GetBlockLightWorld(world, origin);
  WriteBlockLightWorld(world, origin, 0);
  if (old_at_origin > 0)
  {
    remove_q.emplace_back(origin, old_at_origin);
  }

  while (!remove_q.empty())
  {
    const auto [pos, light] = remove_q.front();
    remove_q.pop_front();
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      const glm::ivec3 neighbor = pos + offset;
      if (!ChunkLoadedAt(world, neighbor))
      {
        continue;
      }
      const int nl = GetBlockLightWorld(world, neighbor);
      if (nl > 0 && nl < light)
      {
        WriteBlockLightWorld(world, neighbor, 0);
        remove_q.emplace_back(neighbor, nl);
      }
      else if (nl >= light)
      {
        enqueue_add(neighbor);
      }
    }
  }

  const BlockId origin_id = world.GetBlock(origin);
  const int origin_emission = registry.GetLightEmission(origin_id);
  if (origin_emission > 0)
  {
    // Opaque emitters must keep emission — wiping them left lamps dark until
    // a later non-emitter edit re-seeded the flood.
    WriteBlockLightWorld(world, origin, origin_emission);
    enqueue_add(origin);
  }
  else if (!IsLightTransparent(registry, origin_id))
  {
    WriteBlockLightWorld(world, origin, 0);
  }

  // Seed surviving sources on the face of the edit (torch next to dig).
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    const glm::ivec3 n = origin + offset;
    if (!ChunkLoadedAt(world, n))
    {
      continue;
    }
    const int emission = registry.GetLightEmission(world.GetBlock(n));
    if (emission > GetBlockLightWorld(world, n))
    {
      WriteBlockLightWorld(world, n, emission);
    }
    if (GetBlockLightWorld(world, n) > 0)
    {
      enqueue_add(n);
    }
  }

  while (!add_q.empty())
  {
    const glm::ivec3 pos = add_q.front();
    add_q.pop_front();
    if (!ChunkLoadedAt(world, pos))
    {
      continue;
    }
    const int light = std::max(GetBlockLightWorld(world, pos),
                               registry.GetLightEmission(world.GetBlock(pos)));
    if (light <= 1)
    {
      continue;
    }
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      const glm::ivec3 neighbor = pos + offset;
      if (!ChunkLoadedAt(world, neighbor))
      {
        continue;
      }
      if (!IsLightTransparent(registry, world.GetBlock(neighbor)))
      {
        continue;
      }
      const int next = light - 1;
      if (next > GetBlockLightWorld(world, neighbor))
      {
        WriteBlockLightWorld(world, neighbor, next);
        enqueue_add(neighbor);
      }
    }
  }
}

void RemoveThenPropagateSkyLight(UBlockWorld &world, UBlockRegistry &registry,
                                 glm::ivec3 origin)
{
  std::deque<std::pair<glm::ivec3, int>> remove_q;
  std::deque<glm::ivec3> add_q;
  std::unordered_set<glm::ivec3, IVec3Hash> add_seen;

  const auto enqueue_add = [&](glm::ivec3 p)
  {
    if (add_seen.insert(p).second)
    {
      add_q.push_back(p);
    }
  };

  const int old_at_origin = GetSkyLightWorld(world, origin);
  WriteSkyLightWorld(world, origin, 0);
  if (old_at_origin > 0)
  {
    remove_q.emplace_back(origin, old_at_origin);
  }

  while (!remove_q.empty())
  {
    const auto [pos, light] = remove_q.front();
    remove_q.pop_front();
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      const glm::ivec3 neighbor = pos + offset;
      if (!ChunkLoadedAt(world, neighbor))
      {
        continue;
      }
      const int nl = GetSkyLightWorld(world, neighbor);
      if (nl > 0 && nl < light)
      {
        WriteSkyLightWorld(world, neighbor, 0);
        remove_q.emplace_back(neighbor, nl);
      }
      else if (nl >= light)
      {
        enqueue_add(neighbor);
      }
    }
  }

  // Refresh sky column at edit xz only (neighbors come from horizontal flood).
  const int cy0 = FloorDiv(origin.y - kEditLightRadius, CHUNK_SIZE);
  const int cy1 = FloorDiv(origin.y + kEditLightRadius, CHUNK_SIZE);
  for (int cy = cy0; cy <= cy1; ++cy)
  {
    const glm::ivec3 chunk_coord(FloorDiv(origin.x, CHUNK_SIZE), cy,
                                 FloorDiv(origin.z, CHUNK_SIZE));
    UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
    if (!chunk)
    {
      continue;
    }
    const glm::ivec3 local = UChunkManager::WorldToLocal(
        glm::ivec3(origin.x, cy * CHUNK_SIZE, origin.z));
    PropagateSkylightColumn(world, registry, *chunk, chunk_coord, local.x,
                            local.z);
    for (int ly = 0; ly < CHUNK_SIZE; ++ly)
    {
      const glm::ivec3 cell =
          chunk_coord * CHUNK_SIZE + glm::ivec3(local.x, ly, local.z);
      if (GetSkyLightWorld(world, cell) > 0)
      {
        enqueue_add(cell);
      }
    }
  }

  if (!IsLightTransparent(registry, world.GetBlock(origin)))
  {
    WriteSkyLightWorld(world, origin, 0);
  }
  else if (GetSkyLightWorld(world, origin) > 0)
  {
    enqueue_add(origin);
  }

  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    const glm::ivec3 n = origin + offset;
    if (ChunkLoadedAt(world, n) && GetSkyLightWorld(world, n) > 0)
    {
      enqueue_add(n);
    }
  }

  while (!add_q.empty())
  {
    const glm::ivec3 pos = add_q.front();
    add_q.pop_front();
    if (!ChunkLoadedAt(world, pos))
    {
      continue;
    }
    const int light = GetSkyLightWorld(world, pos);
    if (light <= 1)
    {
      continue;
    }
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      const glm::ivec3 neighbor = pos + offset;
      if (!ChunkLoadedAt(world, neighbor))
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
      if (next > GetSkyLightWorld(world, neighbor))
      {
        WriteSkyLightWorld(world, neighbor, next);
        enqueue_add(neighbor);
      }
    }
  }
}

} // namespace

void RelightChunkCoords(UBlockWorld &world, UBlockRegistry &registry,
                        const std::vector<glm::ivec3> &coords,
                        bool include_block_light, bool include_skylight,
                        bool clear_first = true)
{
  if (clear_first)
  {
    for (const glm::ivec3 &coord : coords)
    {
      if (UChunk *chunk = world.GetChunkManager().GetChunk(coord))
      {
        if (include_skylight && include_block_light)
        {
          ClearChunkLight(*chunk);
        }
        else if (include_skylight)
        {
          ClearChunkSkylight(*chunk);
        }
        else if (include_block_light)
        {
          ClearChunkBlockLight(*chunk);
        }
      }
    }
  }

  if (include_skylight)
  {
    for (const glm::ivec3 &coord : coords)
    {
      UChunk *chunk = world.GetChunkManager().GetChunk(coord);
      if (!chunk)
      {
        continue;
      }
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz)
        {
          PropagateSkylightColumn(world, registry, *chunk, coord, lx, lz);
        }
      }
    }

    for (const glm::ivec3 &coord : coords)
    {
      PropagateSkylightHorizontal(world, registry, coord);
    }
  }

  if (!include_block_light)
  {
    return;
  }

  for (const glm::ivec3 &coord : coords)
  {
    PropagateBlocklight(world, registry, coord);
  }
}

namespace
{

constexpr int kRelightFrontierIterations = kRelightFrontierIterationsFull;

void CollectColumnChunkCoords(const UBlockWorld &world, int world_x, int world_z,
                              int min_y, int max_y,
                              std::unordered_set<glm::ivec3, IVec3Hash> &out)
{
  const glm::ivec3 base =
      UChunkManager::WorldToChunk(glm::ivec3(world_x, min_y, world_z));
  const glm::ivec3 top =
      UChunkManager::WorldToChunk(glm::ivec3(world_x, max_y, world_z));
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int cy = base.y; cy <= top.y; ++cy)
      {
        const glm::ivec3 coord(base.x + dx, cy, base.z + dz);
        if (world.GetChunkManager().HasChunk(coord))
        {
          out.insert(coord);
        }
      }
    }
  }
}

bool FaceHasLitTransparentVoxel(const UBlockWorld &world,
                                const UBlockRegistry &registry,
                                const UChunk &chunk, glm::ivec3 chunk_coord,
                                const glm::ivec3 &neighbor_offset)
{
  const auto check_face = [&](int fixed_axis, int fixed_value, int axis_a,
                              int axis_b)
  {
    for (int a = 0; a < CHUNK_SIZE; ++a)
    {
      for (int b = 0; b < CHUNK_SIZE; ++b)
      {
        glm::ivec3 local(0);
        local[fixed_axis] = fixed_value;
        local[axis_a] = a;
        local[axis_b] = b;
        if (!IsLightTransparent(registry, chunk.GetBlockLocal(local)))
        {
          continue;
        }
        if (chunk.GetSkyLightLocal(local) > 0 ||
            chunk.GetBlockLightLocal(local) > 0)
        {
          return true;
        }
      }
    }
    return false;
  };

  if (neighbor_offset.x == 1)
  {
    return check_face(0, CHUNK_SIZE - 1, 1, 2);
  }
  if (neighbor_offset.x == -1)
  {
    return check_face(0, 0, 1, 2);
  }
  if (neighbor_offset.y == 1)
  {
    return check_face(1, CHUNK_SIZE - 1, 0, 2);
  }
  if (neighbor_offset.y == -1)
  {
    return check_face(1, 0, 0, 2);
  }
  if (neighbor_offset.z == 1)
  {
    return check_face(2, CHUNK_SIZE - 1, 0, 1);
  }
  if (neighbor_offset.z == -1)
  {
    return check_face(2, 0, 0, 1);
  }
  return false;
}

void CollectLitBorderNeighborChunks(
    const UBlockWorld &world, const UBlockRegistry &registry,
    glm::ivec3 chunk_coord,
    const std::unordered_set<glm::ivec3, IVec3Hash> &relit_set,
    std::unordered_set<glm::ivec3, IVec3Hash> &frontier)
{
  const UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return;
  }
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    const glm::ivec3 neighbor_coord = chunk_coord + offset;
    if (relit_set.count(neighbor_coord) ||
        !world.GetChunkManager().HasChunk(neighbor_coord))
    {
      continue;
    }
    if (FaceHasLitTransparentVoxel(world, registry, *chunk, chunk_coord,
                                   offset))
    {
      frontier.insert(neighbor_coord);
    }
  }
}

std::unordered_set<glm::ivec3, IVec3Hash>
RelightChunkSetWithFrontier(UBlockWorld &world, UBlockRegistry &registry,
                            std::unordered_set<glm::ivec3, IVec3Hash>
                                initial_coords,
                            bool include_block_light, int frontier_iterations,
                            bool *out_frontier_unfinished,
                            bool include_skylight = true)
{
  if (initial_coords.empty())
  {
    if (out_frontier_unfinished)
    {
      *out_frontier_unfinished = false;
    }
    return {};
  }

  std::vector<glm::ivec3> batch(initial_coords.begin(), initial_coords.end());
  RelightChunkCoords(world, registry, batch, include_block_light,
                     include_skylight);

  std::unordered_set<glm::ivec3, IVec3Hash> relit_set =
      std::move(initial_coords);

  bool frontier_unfinished = false;
  for (int iter = 0; iter < frontier_iterations; ++iter)
  {
    std::unordered_set<glm::ivec3, IVec3Hash> frontier;
    for (const glm::ivec3 &coord : relit_set)
    {
      CollectLitBorderNeighborChunks(world, registry, coord, relit_set,
                                     frontier);
    }
    if (frontier.empty())
    {
      frontier_unfinished = false;
      break;
    }
    batch.assign(frontier.begin(), frontier.end());
    RelightChunkCoords(world, registry, batch, include_block_light,
                       include_skylight);
    relit_set.insert(frontier.begin(), frontier.end());
    frontier_unfinished = (iter == frontier_iterations - 1);
  }
  if (out_frontier_unfinished)
  {
    *out_frontier_unfinished = frontier_unfinished;
  }
  return relit_set;
}

void CollectEditNeighborhoodChunkCoords(
    const UBlockWorld &world,
    const std::vector<glm::ivec3> &block_positions,
    std::unordered_set<glm::ivec3, IVec3Hash> &out)
{
  // Fast edit path only: edit column ±1 cy. Full 3×3 Clear was 100–500ms and
  // still left seams to async player-relight; keep FastRelight cheap.
  for (const glm::ivec3 &block_pos : block_positions)
  {
    const glm::ivec3 center = UChunkManager::WorldToChunk(block_pos);
    for (int dy = -1; dy <= 1; ++dy)
    {
      const glm::ivec3 layer(center.x, center.y + dy, center.z);
      if (world.GetChunkManager().HasChunk(layer))
      {
        out.insert(layer);
      }
    }
  }
}

} // namespace

void RelightChunk(UBlockWorld &world, UBlockRegistry &registry,
                  glm::ivec3 chunk_coord, bool include_block_light,
                  bool include_skylight)
{
  if (!world.GetChunkManager().HasChunk(chunk_coord))
  {
    return;
  }
  RelightChunkCoords(world, registry, {chunk_coord}, include_block_light,
                     include_skylight);
}

void RelightChunkAfterGpuSkySeed(UBlockWorld &world, UBlockRegistry &registry,
                                 glm::ivec3 chunk_coord,
                                 bool include_block_light)
{
  if (!world.GetChunkManager().HasChunk(chunk_coord))
  {
    return;
  }
  // Sky columns already written by ApplyGpuSkylightSeedToChunk.
  PropagateSkylightHorizontal(world, registry, chunk_coord);
  if (include_block_light)
  {
    PropagateBlocklight(world, registry, chunk_coord);
  }
}

void RelightChunkBlockLight(UBlockWorld &world, UBlockRegistry &registry,
                            glm::ivec3 chunk_coord)
{
  RelightChunkCoords(world, registry, {chunk_coord}, true, false);
}

void RelightBlocksAroundLocal(UBlockWorld &world, UBlockRegistry &registry,
                              const std::vector<glm::ivec3> &block_positions,
                              bool clear_first)
{
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  CollectEditNeighborhoodChunkCoords(world, block_positions, coords);
  if (coords.empty())
  {
    return;
  }
  const std::vector<glm::ivec3> batch(coords.begin(), coords.end());
  RelightChunkCoords(world, registry, batch, true, true, clear_first);
}

void RelightBlocksAroundEdit(UBlockWorld &world, UBlockRegistry &registry,
                             const std::vector<glm::ivec3> &block_positions)
{
  for (const glm::ivec3 &pos : block_positions)
  {
    if (!ChunkLoadedAt(world, pos))
    {
      continue;
    }
    RemoveThenPropagateBlockLight(world, registry, pos);
    RemoveThenPropagateSkyLight(world, registry, pos);
  }
}

void RelightChunksAround(UBlockWorld &world, UBlockRegistry &registry,
                         glm::ivec3 block_pos, int max_world_y)
{
  (void)max_world_y;
  RelightBlocksAroundLocal(world, registry, {block_pos});
}

std::vector<glm::ivec3>
RelightBlocksAroundAll(UBlockWorld &world, UBlockRegistry &registry,
                       const std::vector<glm::ivec3> &block_positions,
                       int max_world_y)
{
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  for (const glm::ivec3 &block_pos : block_positions)
  {
    CollectColumnChunkCoords(world, block_pos.x, block_pos.z, 0, max_world_y,
                             coords);
  }
  const std::unordered_set<glm::ivec3, IVec3Hash> relit =
      RelightChunkSetWithFrontier(world, registry, std::move(coords), true,
                                  kRelightFrontierIterationsFull, nullptr);
  return {relit.begin(), relit.end()};
}

RelightFrontierOutcome RelightBlocksAroundAllEx(
    UBlockWorld &world, UBlockRegistry &registry,
    const std::vector<glm::ivec3> &block_positions, int min_world_y,
    int max_world_y, bool include_block_light, int frontier_iterations)
{
  RelightFrontierOutcome outcome;
  if (block_positions.empty())
  {
    return outcome;
  }
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  for (const glm::ivec3 &block_pos : block_positions)
  {
    CollectColumnChunkCoords(world, block_pos.x, block_pos.z, min_world_y,
                             max_world_y, coords);
  }
  const std::unordered_set<glm::ivec3, IVec3Hash> relit =
      RelightChunkSetWithFrontier(world, registry, std::move(coords),
                                  include_block_light, frontier_iterations,
                                  &outcome.frontier_unfinished);
  outcome.relit_chunks.assign(relit.begin(), relit.end());
  return outcome;
}

void RelightColumnWithFrontier(UBlockWorld &world, UBlockRegistry &registry,
                               int world_x, int world_z, int min_y, int max_y,
                               bool include_block_light, bool include_skylight,
                               std::vector<glm::ivec3> *out_relit_chunks)
{
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  CollectColumnChunkCoords(world, world_x, world_z, min_y, max_y, coords);
  const std::unordered_set<glm::ivec3, IVec3Hash> relit =
      RelightChunkSetWithFrontier(world, registry, std::move(coords),
                                  include_block_light,
                                  kRelightFrontierIterationsFull, nullptr,
                                  include_skylight);
  if (!out_relit_chunks)
  {
    return;
  }
  out_relit_chunks->assign(relit.begin(), relit.end());
}

void RelightColumn(UBlockWorld &world, UBlockRegistry &registry, int world_x,
                   int world_z, int min_y, int max_y, bool include_block_light,
                   bool include_skylight)
{
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  CollectColumnChunkCoords(world, world_x, world_z, min_y, max_y, coords);
  std::vector<glm::ivec3> coord_list(coords.begin(), coords.end());
  if (!coord_list.empty())
  {
    RelightChunkCoords(world, registry, coord_list, include_block_light,
                       include_skylight);
  }
}

void RelightAllLoadedChunks(UBlockWorld &world, UBlockRegistry &registry)
{
  world.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      { RelightChunk(world, registry, chunk.GetCoord()); });
}

} // namespace cutum
