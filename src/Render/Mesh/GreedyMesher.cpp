#include "Render/Mesh/GreedyMesher.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"

#include "Render/Mesh/ChunkMeshSnapshot.h"

#include "World/Mesh/IUChunkMeshReader.h"

#include "World/Chunks/Chunk.h"

#include "World/Chunks/ChunkManager.h"

#include "World/Core/BlockWorld.h"

#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidBlockResolver.h"

#include <algorithm>

#include <cstring>

namespace cutum

{

namespace

{

class UBlockWorldChunkReader : public IUChunkMeshReader

{

public:
  UBlockWorldChunkReader(const UBlockWorld &world, glm::ivec3 chunk_coord,

                         const UChunk *chunk)

      : World(world), ChunkCoordValue(chunk_coord), Chunk(chunk)

  {
  }

  glm::ivec3 ChunkCoord() const override { return ChunkCoordValue; }

  BlockId GetBlockLocal(glm::ivec3 local) const override

  {

    return Chunk ? Chunk->GetBlockLocal(local) : BLOCK_AIR;
  }

  BlockId GetBlock(glm::ivec3 world_pos) const override

  {

    return World.GetBlock(world_pos);
  }

  uint8_t GetFluidPackedLocal(glm::ivec3 local) const override

  {

    return Chunk ? PackFluidCellState(Chunk->GetFluidLocal(local)) : 0;
  }

  FluidCellState GetFluid(glm::ivec3 world_pos) const override
  {
    return World.GetFluidState(world_pos);
  }

private:
  const UBlockWorld &World;

  glm::ivec3 ChunkCoordValue;

  const UChunk *Chunk;
};

class USnapshotChunkReader : public IUChunkMeshReader

{

public:
  explicit USnapshotChunkReader(const ChunkMeshSnapshot &snapshot)

      : Snapshot(snapshot)

  {
  }

  glm::ivec3 ChunkCoord() const override { return Snapshot.coord; }

  BlockId GetBlockLocal(glm::ivec3 local) const override

  {

    return Snapshot.GetBlockLocal(local);
  }

  BlockId GetBlock(glm::ivec3 world_pos) const override

  {

    return Snapshot.GetBlock(world_pos);
  }

  uint8_t GetFluidPackedLocal(glm::ivec3 local) const override

  {

    return Snapshot.GetFluidPackedLocal(local);
  }

  FluidCellState GetFluid(glm::ivec3 world_pos) const override

  {

    return Snapshot.GetFluid(world_pos);
  }

private:
  const ChunkMeshSnapshot &Snapshot;
};

bool CellHasRenderableFluid(IUChunkMeshReader &reader, UBlockRegistry &registry,
                            glm::ivec3 world_pos)
{
  const BlockId id = reader.GetBlock(world_pos);
  if (registry.IsLiquid(id))
  {
    return true;
  }
  if (registry.IsFluidPermeable(id) &&
      FluidCellHasActiveFluid(PackFluidCellState(reader.GetFluid(world_pos))))
  {
    return true;
  }
  return false;
}

bool NeighborHidesFace(IUChunkMeshReader &reader, UBlockRegistry &registry,

                       BlockId face_id, glm::ivec3 block_pos,

                       glm::ivec3 neighbor_offset)

{

  const glm::ivec3 neighbor_pos = block_pos + neighbor_offset;

  const BlockId neighbor = reader.GetBlock(neighbor_pos);

  const BlockRenderStyle face_style = registry.GetRenderStyle(face_id);

  if (neighbor == BLOCK_AIR)

  {

    return false;
  }

  if (face_style == BlockRenderStyle::Cutout)

  {

    if (registry.GetRenderStyle(neighbor) == BlockRenderStyle::Cutout)

    {

      return false;
    }
  }

  if (neighbor == face_id && !registry.BlocksMovement(face_id))

  {

    return true;
  }

  if (face_style == BlockRenderStyle::Fluid &&
      CellHasRenderableFluid(reader, registry, neighbor_pos))
  {
    return registry.IsLiquid(neighbor) && neighbor == face_id;
  }

  const bool face_transparent = registry.IsTransparent(face_id);

  const bool neighbor_transparent = registry.IsTransparent(neighbor);

  if (face_transparent && neighbor_transparent)
  {
    if (face_style == BlockRenderStyle::Fluid &&
        registry.IsFluidPermeable(neighbor))
    {
      return false;
    }
    return true;
  }

  const BlockRenderStyle neighbor_render_style =

      registry.GetRenderStyle(neighbor);

  if (face_style == BlockRenderStyle::Fluid && !neighbor_transparent)

  {

    return true;
  }

  if (!face_transparent && neighbor_render_style == BlockRenderStyle::Fluid)

  {

    return false;
  }

  if (!face_transparent && neighbor_transparent &&

      registry.BlocksMovement(neighbor))

  {

    if (neighbor_render_style == BlockRenderStyle::Fluid)

    {

      return false;
    }

    const glm::ivec3 beyond_pos = neighbor_pos + neighbor_offset;

    const glm::ivec3 before_pos = block_pos - neighbor_offset;

    const BlockId beyond = reader.GetBlock(beyond_pos);

    const BlockId before = reader.GetBlock(before_pos);

    const bool beyond_open =

        (beyond == BLOCK_AIR || !registry.BlocksMovement(beyond));

    const bool before_solid =

        (before != BLOCK_AIR && registry.BlocksMovement(before));

    if (beyond_open && before_solid)

    {

      return false;
    }

    if (beyond_open)

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

bool WaterloggedNeighborHidesFace(IUChunkMeshReader &reader,
                                  UBlockRegistry &registry, BlockId fluid_id,
                                  glm::ivec3 block_pos, glm::ivec3 neighbor_offset)
{
  const glm::ivec3 neighbor_pos = block_pos + neighbor_offset;
  if (!CellHasRenderableFluid(reader, registry, neighbor_pos))
  {
    return false;
  }
  return NeighborHidesFace(reader, registry, fluid_id, block_pos, neighbor_offset);
}

void AppendWaterloggedFluidQuads(IUChunkMeshReader &reader,
                               UBlockRegistry &registry,
                               std::vector<GreedyQuad> &quads, int max_mesh_y)
{
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return;
  }

  const glm::ivec3 chunk_coord = reader.ChunkCoord();
  BlockId mask[CHUNK_SIZE][CHUNK_SIZE];
  uint8_t fluid_mask[CHUNK_SIZE][CHUNK_SIZE];

  for (int axis = 0; axis < 3; ++axis)
  {
    const int u_axis = (axis + 1) % 3;
    const int v_axis = (axis + 2) % 3;
    for (int sign = -1; sign <= 1; sign += 2)
    {
      for (int slice = 0; slice < CHUNK_SIZE; ++slice)
      {
        if (axis == 1 && max_mesh_y >= 0 && slice > max_mesh_y)
        {
          continue;
        }

        std::memset(mask, 0, sizeof(mask));
        std::memset(fluid_mask, 0, sizeof(fluid_mask));

        for (int v = 0; v < CHUNK_SIZE; ++v)
        {
          for (int u = 0; u < CHUNK_SIZE; ++u)
          {
            glm::ivec3 local(0);
            local[axis] = slice;
            local[u_axis] = u;
            local[v_axis] = v;

            const glm::ivec3 world_pos(chunk_coord.x * CHUNK_SIZE + local.x,
                                       chunk_coord.y * CHUNK_SIZE + local.y,
                                       chunk_coord.z * CHUNK_SIZE + local.z);
            const BlockId id = reader.GetBlockLocal(local);
            if (!registry.IsFluidPermeable(id))
            {
              continue;
            }
            const uint8_t packed = PackFluidCellState(reader.GetFluid(world_pos));
            if (!FluidCellHasActiveFluid(packed))
            {
              continue;
            }

            const BlockId fluid_id =
                UFluidBlockResolver::ResolveFluidBlockIdForMesh(
                    reader, *definitions, world_pos);
            if (fluid_id == BLOCK_AIR)
            {
              continue;
            }

            glm::ivec3 neighbor_offset(0);
            neighbor_offset[axis] = sign;
            if (WaterloggedNeighborHidesFace(reader, registry, fluid_id,
                                               world_pos, neighbor_offset))
            {
              continue;
            }

            mask[v][u] = fluid_id;
            fluid_mask[v][u] = packed;
          }
        }

        for (int v = 0; v < CHUNK_SIZE; ++v)
        {
          for (int u = 0; u < CHUNK_SIZE; ++u)
          {
            const BlockId id = mask[v][u];
            if (id == BLOCK_AIR)
            {
              continue;
            }

            int width = 1;
            while (u + width < CHUNK_SIZE && mask[v][u + width] == id &&
                   fluid_mask[v][u + width] == fluid_mask[v][u])
            {
              ++width;
            }

            int height = 1;
            bool done = false;
            while (v + height < CHUNK_SIZE && !done)
            {
              for (int k = 0; k < width; ++k)
              {
                if (mask[v + height][u + k] != id ||
                    fluid_mask[v + height][u + k] != fluid_mask[v][u])
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
            quad.Id = id;
            quad.faceSign = sign;
            quad.FluidPacked = fluid_mask[v][u];
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
}

int MaxMeshLocalY(IUChunkMeshReader &reader, UBlockRegistry &registry)

{

  int max_y = -1;
  const glm::ivec3 chunk_coord = reader.ChunkCoord();

  for (int z = 0; z < CHUNK_SIZE; ++z)

  {

    for (int y = 0; y < CHUNK_SIZE; ++y)

    {

      for (int x = 0; x < CHUNK_SIZE; ++x)

      {

        const glm::ivec3 local(x, y, z);
        const BlockId id = reader.GetBlockLocal(local);

        if (id != BLOCK_AIR &&

            registry.GetRenderStyle(id) != BlockRenderStyle::Cross)

        {

          max_y = std::max(max_y, y);
        }
        else if (registry.IsFluidPermeable(id))
        {
          const glm::ivec3 world_pos(chunk_coord.x * CHUNK_SIZE + local.x,
                                     chunk_coord.y * CHUNK_SIZE + local.y,
                                     chunk_coord.z * CHUNK_SIZE + local.z);
          if (PackFluidCellState(reader.GetFluid(world_pos)) != 0)
          {
            max_y = std::max(max_y, y);
          }
        }
      }
    }
  }

  return max_y;
}

std::vector<GreedyQuad> BuildChunkMeshImpl(IUChunkMeshReader &reader,

                                           UBlockRegistry &registry)

{

  std::vector<GreedyQuad> quads;

  quads.reserve(512);

  const glm::ivec3 chunk_coord = reader.ChunkCoord();

  const int max_mesh_y = MaxMeshLocalY(reader, registry);

  BlockId mask[CHUNK_SIZE][CHUNK_SIZE];

  uint8_t fluid_mask[CHUNK_SIZE][CHUNK_SIZE];

  for (int axis = 0; axis < 3; ++axis)

  {

    const int u_axis = (axis + 1) % 3;

    const int v_axis = (axis + 2) % 3;

    for (int sign = -1; sign <= 1; sign += 2)

    {

      for (int slice = 0; slice < CHUNK_SIZE; ++slice)

      {

        if (axis == 1 && max_mesh_y >= 0 && slice > max_mesh_y)

        {

          continue;
        }

        std::memset(mask, 0, sizeof(mask));

        std::memset(fluid_mask, 0, sizeof(fluid_mask));

        for (int v = 0; v < CHUNK_SIZE; ++v)

        {

          for (int u = 0; u < CHUNK_SIZE; ++u)

          {

            glm::ivec3 local(0);

            local[axis] = slice;

            local[u_axis] = u;

            local[v_axis] = v;

            const glm::ivec3 world_pos(chunk_coord.x * CHUNK_SIZE + local.x,

                                       chunk_coord.y * CHUNK_SIZE + local.y,

                                       chunk_coord.z * CHUNK_SIZE + local.z);

            const BlockId id = reader.GetBlockLocal(local);

            if (id == BLOCK_AIR)

            {

              continue;
            }

            if (registry.GetRenderStyle(id) == BlockRenderStyle::Cross)

            {

              continue;
            }

            glm::ivec3 neighbor_offset(0);

            neighbor_offset[axis] = sign;

            if (NeighborHidesFace(reader, registry, id, world_pos,

                                  neighbor_offset))

            {

              continue;
            }

            mask[v][u] = id;

            fluid_mask[v][u] =
                registry.IsLiquid(id)
                    ? static_cast<uint8_t>(1)
                    : PackFluidCellState(reader.GetFluid(world_pos));
          }
        }

        for (int v = 0; v < CHUNK_SIZE; ++v)

        {

          for (int u = 0; u < CHUNK_SIZE; ++u)

          {

            const BlockId id = mask[v][u];

            if (id == BLOCK_AIR)

            {

              continue;
            }

            int width = 1;

            while (u + width < CHUNK_SIZE && mask[v][u + width] == id &&

                   fluid_mask[v][u + width] == fluid_mask[v][u])

            {

              ++width;
            }

            int height = 1;

            bool done = false;

            while (v + height < CHUNK_SIZE && !done)

            {

              for (int k = 0; k < width; ++k)

              {

                if (mask[v + height][u + k] != id ||

                    fluid_mask[v + height][u + k] != fluid_mask[v][u])

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

            quad.Id = id;

            quad.faceSign = sign;

            quad.FluidPacked = fluid_mask[v][u];

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

  AppendWaterloggedFluidQuads(reader, registry, quads, max_mesh_y);

  return quads;
}

} // namespace

std::vector<GreedyQuad> UGreedyMesher::BuildChunkMesh(const UBlockWorld &world,

                                                      glm::ivec3 chunk_coord,

                                                      UBlockRegistry &registry)

{

  const UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);

  if (!chunk)

  {

    return {};
  }

  UBlockWorldChunkReader reader(world, chunk_coord, chunk);

  return BuildChunkMeshImpl(reader, registry);
}

std::vector<GreedyQuad>

UGreedyMesher::BuildChunkMesh(const ChunkMeshSnapshot &snapshot,

                              UBlockRegistry &registry)

{

  USnapshotChunkReader reader(snapshot);

  return BuildChunkMeshImpl(reader, registry);
}

} // namespace cutum
