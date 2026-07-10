#include "World/Lighting/ChunkRelightSnapshot.h"

#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/LightUtil.h"
#include "World/Math/GridMath.h"

#include <deque>
#include <unordered_set>

namespace cutum
{

namespace
{

constexpr int kSkyScanAboveBlocks = 15 * CHUNK_SIZE;

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

bool InChunkLocal(glm::ivec3 local)
{
  return local.x >= 0 && local.x < CHUNK_SIZE && local.y >= 0 &&
         local.y < CHUNK_SIZE && local.z >= 0 && local.z < CHUNK_SIZE;
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
  if (registry.IsLiquid(id))
  {
    return 1;
  }
  return 0;
}

bool SharesChunkFace(glm::ivec3 world_pos, glm::ivec3 chunk_coord)
{
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  const glm::ivec3 local = world_pos - origin;
  if (!InChunkLocal(local))
  {
    return false;
  }
  return local.x == 0 || local.x == CHUNK_SIZE - 1 || local.y == 0 ||
         local.y == CHUNK_SIZE - 1 || local.z == 0 || local.z == CHUNK_SIZE - 1;
}

void PropagateSkylightColumn(UChunkRelightSnapshot &grid,
                             const UBlockRegistry &registry,
                             glm::ivec3 chunk_coord, int local_x, int local_z)
{
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  const int world_x = origin.x + local_x;
  const int world_z = origin.z + local_z;
  const int top_y = origin.y + CHUNK_SIZE - 1;
  int incoming = kMaxLightLevel;
  for (int y = top_y + 1; y < top_y + kSkyScanAboveBlocks; ++y)
  {
    const BlockId id = grid.GetBlock(glm::ivec3(world_x, y, world_z));
    if (!IsLightTransparent(registry, id))
    {
      incoming = 0;
      break;
    }
  }
  for (int y = top_y; y >= origin.y; --y)
  {
    const glm::ivec3 world_pos(world_x, y, world_z);
    const BlockId id = grid.GetBlock(world_pos);
    const glm::ivec3 local(local_x, y - origin.y, local_z);
    if (IsLightTransparent(registry, id))
    {
      grid.WriteSkyLight(world_pos, incoming);
    }
    incoming = std::max(0, incoming - VerticalSkylightStepCost(registry, id));
  }
}

void SeedSkylightHorizontalQueue(UChunkRelightSnapshot &grid,
                                 const UBlockRegistry &registry,
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
    if (!IsLightTransparent(registry, grid.GetBlock(world_pos)))
    {
      return;
    }
    queue.push_back(world_pos);
  };

  if (grid.HasChunk(chunk_coord))
  {
    for (int ly = 0; ly < CHUNK_SIZE; ++ly)
    {
      for (int lz = 0; lz < CHUNK_SIZE; ++lz)
      {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx)
        {
          const glm::ivec3 local(lx, ly, lz);
          try_seed(origin + local,
                   grid.GetSkyLightLocal(chunk_coord, local));
        }
      }
    }
  }

  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    const glm::ivec3 neighbor_coord = chunk_coord + offset;
    if (!grid.HasChunk(neighbor_coord))
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
          try_seed(world_pos,
                   grid.GetSkyLightLocal(neighbor_coord, local));
        }
      }
    }
  }
}

void PropagateSkylightHorizontal(UChunkRelightSnapshot &grid,
                                 const UBlockRegistry &registry,
                                 glm::ivec3 chunk_coord)
{
  std::deque<glm::ivec3> queue;
  SeedSkylightHorizontalQueue(grid, registry, chunk_coord, queue);
  while (!queue.empty())
  {
    const glm::ivec3 pos = queue.front();
    queue.pop_front();
    const int light = grid.GetSkyLight(pos);
    if (light <= 1)
    {
      continue;
    }
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      const glm::ivec3 neighbor = pos + offset;
      const BlockId neighbor_id = grid.GetBlock(neighbor);
      if (!IsLightTransparent(registry, neighbor_id))
      {
        continue;
      }
      const int next =
          std::max(0, light - SkylightStepCost(registry, neighbor_id));
      if (next <= grid.GetSkyLight(neighbor))
      {
        continue;
      }
      grid.WriteSkyLight(neighbor, next);
      queue.push_back(neighbor);
    }
  }
}

void PropagateBlocklight(UChunkRelightSnapshot &grid,
                         const UBlockRegistry &registry,
                         glm::ivec3 chunk_coord)
{
  std::deque<std::pair<glm::ivec3, int>> queue;
  const auto seed_chunk = [&](glm::ivec3 seed_coord)
  {
    if (!grid.HasChunk(seed_coord))
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
          const BlockId id = grid.GetBlock(seed_origin + local);
          const int emission = registry.GetLightEmission(id);
          if (emission > 0)
          {
            queue.emplace_back(seed_origin + local, emission);
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
    if (light <= grid.GetBlockLight(pos))
    {
      continue;
    }
    grid.WriteBlockLight(pos, light);
    if (light <= 1)
    {
      continue;
    }
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      const glm::ivec3 neighbor = pos + offset;
      if (!IsLightTransparent(registry, grid.GetBlock(neighbor)))
      {
        continue;
      }
      const int next = light - 1;
      if (next > grid.GetBlockLight(neighbor))
      {
        queue.emplace_back(neighbor, next);
      }
    }
  }
}

void RelightChunkCoordsOnGrid(UChunkRelightSnapshot &grid,
                              const UBlockRegistry &registry,
                              const std::vector<glm::ivec3> &coords,
                              bool include_block_light, bool include_skylight)
{
  for (const glm::ivec3 &coord : coords)
  {
    if (!grid.HasChunk(coord))
    {
      continue;
    }
    if (include_skylight && include_block_light)
    {
      grid.ClearChunkLight(coord);
    }
    else if (include_skylight)
    {
      for (int ly = 0; ly < CHUNK_SIZE; ++ly)
      {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz)
        {
          for (int lx = 0; lx < CHUNK_SIZE; ++lx)
          {
            grid.WriteSkyLight(coord * CHUNK_SIZE + glm::ivec3(lx, ly, lz), 0);
          }
        }
      }
    }
    else if (include_block_light)
    {
      for (int ly = 0; ly < CHUNK_SIZE; ++ly)
      {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz)
        {
          for (int lx = 0; lx < CHUNK_SIZE; ++lx)
          {
            grid.WriteBlockLight(coord * CHUNK_SIZE + glm::ivec3(lx, ly, lz), 0);
          }
        }
      }
    }
  }
  if (include_skylight)
  {
    for (const glm::ivec3 &coord : coords)
    {
      if (!grid.HasChunk(coord))
      {
        continue;
      }
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz)
        {
          PropagateSkylightColumn(grid, registry, coord, lx, lz);
        }
      }
    }
    for (const glm::ivec3 &coord : coords)
    {
      PropagateSkylightHorizontal(grid, registry, coord);
    }
  }
  if (!include_block_light)
  {
    return;
  }
  for (const glm::ivec3 &coord : coords)
  {
    PropagateBlocklight(grid, registry, coord);
  }
}

bool FaceHasLitTransparentVoxel(UChunkRelightSnapshot &grid,
                                const UBlockRegistry &registry,
                                glm::ivec3 chunk_coord,
                                const glm::ivec3 &neighbor_offset)
{
  if (!grid.HasChunk(chunk_coord))
  {
    return false;
  }
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
        const glm::ivec3 world = chunk_coord * CHUNK_SIZE + local;
        if (!IsLightTransparent(registry, grid.GetBlock(world)))
        {
          continue;
        }
        if (grid.GetSkyLight(world) > 0 || grid.GetBlockLight(world) > 0)
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

} // namespace

BlockId UChunkRelightSnapshot::GetBlock(glm::ivec3 world_pos) const
{
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(world_pos);
  const glm::ivec3 local = UChunkManager::WorldToLocal(world_pos);
  const auto it = Blocks.find(chunk_coord);
  if (it != Blocks.end() && InChunkLocal(local))
  {
    return it->second[local.x + CHUNK_SIZE * (local.y + CHUNK_SIZE * local.z)];
  }
  const auto shell = ShellBlocks.find(world_pos);
  if (shell != ShellBlocks.end())
  {
    return shell->second;
  }
  return BLOCK_AIR;
}

bool UChunkRelightSnapshot::HasChunk(glm::ivec3 chunk_coord) const
{
  return Blocks.count(chunk_coord) > 0;
}

int UChunkRelightSnapshot::GetSkyLight(glm::ivec3 world_pos) const
{
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(world_pos);
  const glm::ivec3 local = UChunkManager::WorldToLocal(world_pos);
  return GetSkyLightLocal(chunk_coord, local);
}

int UChunkRelightSnapshot::GetBlockLight(glm::ivec3 world_pos) const
{
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(world_pos);
  const glm::ivec3 local = UChunkManager::WorldToLocal(world_pos);
  return GetBlockLightLocal(chunk_coord, local);
}

void UChunkRelightSnapshot::WriteSkyLight(glm::ivec3 world_pos, int level)
{
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(world_pos);
  const glm::ivec3 local = UChunkManager::WorldToLocal(world_pos);
  SetLightLocal(chunk_coord, local, level, GetBlockLightLocal(chunk_coord, local));
}

void UChunkRelightSnapshot::WriteBlockLight(glm::ivec3 world_pos, int level)
{
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(world_pos);
  const glm::ivec3 local = UChunkManager::WorldToLocal(world_pos);
  SetLightLocal(chunk_coord, local, GetSkyLightLocal(chunk_coord, local), level);
}

int UChunkRelightSnapshot::GetSkyLightLocal(glm::ivec3 chunk_coord,
                                            glm::ivec3 local) const
{
  const auto it = Light.find(chunk_coord);
  if (it == Light.end() || !InChunkLocal(local))
  {
    return 0;
  }
  const uint8_t packed =
      it->second[local.x + CHUNK_SIZE * (local.y + CHUNK_SIZE * local.z)];
  return packed & 0x0F;
}

int UChunkRelightSnapshot::GetBlockLightLocal(glm::ivec3 chunk_coord,
                                              glm::ivec3 local) const
{
  const auto it = Light.find(chunk_coord);
  if (it == Light.end() || !InChunkLocal(local))
  {
    return 0;
  }
  const uint8_t packed =
      it->second[local.x + CHUNK_SIZE * (local.y + CHUNK_SIZE * local.z)];
  return (packed >> 4) & 0x0F;
}

void UChunkRelightSnapshot::ClearChunkLight(glm::ivec3 chunk_coord)
{
  auto it = Light.find(chunk_coord);
  if (it != Light.end())
  {
    it->second.fill(0);
  }
}

void UChunkRelightSnapshot::SetLightLocal(glm::ivec3 chunk_coord,
                                          glm::ivec3 local, int sky,
                                          int block_level)
{
  auto it = Light.find(chunk_coord);
  if (it == Light.end() || !InChunkLocal(local))
  {
    return;
  }
  const int idx = local.x + CHUNK_SIZE * (local.y + CHUNK_SIZE * local.z);
  it->second[idx] = static_cast<uint8_t>((std::clamp(block_level, 0, 15) << 4) |
                                         std::clamp(sky, 0, 15));
}

UChunkRelightSnapshot UChunkRelightSnapshot::Capture(const UBlockWorld &world,
                                                    const RelightJobSpec &spec)
{
  UChunkRelightSnapshot snapshot;
  snapshot.Spec = spec;
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  for (const glm::ivec3 &block_pos : spec.block_positions)
  {
    CollectColumnChunkCoords(world, block_pos.x, block_pos.z, spec.min_world_y,
                             spec.max_world_y, coords);
  }
  for (const glm::ivec3 &coord : coords)
  {
    const UChunk *chunk = world.GetChunkManager().GetChunk(coord);
    if (!chunk)
    {
      continue;
    }
    snapshot.Blocks[coord] = chunk->GetData();
    snapshot.Light[coord] = chunk->GetLightData();
    const glm::ivec3 origin = coord * CHUNK_SIZE;
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      const glm::ivec3 neighbor_coord = coord + offset;
      if (coords.count(neighbor_coord))
      {
        continue;
      }
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
            if (!SharesChunkFace(world_pos, coord))
            {
              continue;
            }
            snapshot.ShellBlocks[world_pos] = neighbor->GetBlockLocal(local);
          }
        }
      }
    }
  }
  return snapshot;
}

RelightComputeResult
UChunkRelightSnapshot::Compute(const UBlockRegistry &registry) const
{
  UChunkRelightSnapshot grid = *this;
  RelightComputeResult result;
  result.job_id = Spec.job_id;
  result.source_block_positions = Spec.block_positions;
  if (grid.Blocks.empty())
  {
    return result;
  }

  std::unordered_set<glm::ivec3, IVec3Hash> relit_set;
  for (const auto &entry : grid.Blocks)
  {
    relit_set.insert(entry.first);
  }
  std::vector<glm::ivec3> batch(relit_set.begin(), relit_set.end());
  RelightChunkCoordsOnGrid(grid, registry, batch, Spec.include_block_light,
                           Spec.include_skylight);

  bool frontier_unfinished = false;
  for (int iter = 0; iter < Spec.frontier_iterations; ++iter)
  {
    std::unordered_set<glm::ivec3, IVec3Hash> frontier;
    for (const glm::ivec3 &coord : relit_set)
    {
      for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
      {
        const glm::ivec3 neighbor_coord = coord + offset;
        if (relit_set.count(neighbor_coord) || !grid.HasChunk(neighbor_coord))
        {
          continue;
        }
        if (FaceHasLitTransparentVoxel(grid, registry, coord, offset))
        {
          frontier.insert(neighbor_coord);
        }
      }
    }
    if (frontier.empty())
    {
      break;
    }
    batch.assign(frontier.begin(), frontier.end());
    RelightChunkCoordsOnGrid(grid, registry, batch, Spec.include_block_light,
                           Spec.include_skylight);
    relit_set.insert(frontier.begin(), frontier.end());
    frontier_unfinished = (iter == Spec.frontier_iterations - 1);
  }
  result.frontier_unfinished = frontier_unfinished;
  result.chunks.reserve(grid.Light.size());
  for (const auto &entry : grid.Light)
  {
    RelightChunkLightData chunk_data;
    chunk_data.coord = entry.first;
    chunk_data.light_packed = entry.second;
    result.chunks.push_back(std::move(chunk_data));
  }
  return result;
}

} // namespace cutum
