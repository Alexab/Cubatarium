#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/MeshCaptureToken.h"
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

/// Map local offset (relative to chunk origin) onto a single face cell.
/// Returns false for interior cells, edges/corners, or farther shell.
bool TryShellIndex(glm::ivec3 local, int &face, int &cell)
{
  int outside_axis = -1;
  int outside_sign = 0;
  for (int axis = 0; axis < 3; ++axis)
  {
    if (local[axis] >= 0 && local[axis] < CHUNK_SIZE)
    {
      continue;
    }
    if (outside_axis >= 0)
    {
      return false;
    }
    if (local[axis] == -1)
    {
      outside_axis = axis;
      outside_sign = -1;
    }
    else if (local[axis] == CHUNK_SIZE)
    {
      outside_axis = axis;
      outside_sign = 1;
    }
    else
    {
      return false;
    }
  }
  if (outside_axis < 0)
  {
    return false;
  }
  const int u_axis = (outside_axis + 1) % 3;
  const int v_axis = (outside_axis + 2) % 3;
  face = outside_axis * 2 + (outside_sign > 0 ? 1 : 0);
  cell = local[u_axis] + local[v_axis] * CHUNK_SIZE;
  return true;
}

int ShellFlatIndex(int face, int cell)
{
  return face * ChunkMeshSnapshot::kShellFaceCells + cell;
}

} // namespace

ChunkMeshSnapshot ChunkMeshSnapshot::Capture(
    const UBlockWorld &world, glm::ivec3 chunkCoord, uint64_t sourceRevision,
    NeighborVisualDrawableFn neighbor_drawable, void *neighbor_drawable_ctx)
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
  snapshot.light_packed = chunk->GetLightData();

  const glm::ivec3 origin = snapshot.ChunkOrigin();
  for (int axis = 0; axis < 3; ++axis)
  {
    for (int sign = -1; sign <= 1; sign += 2)
    {
      const int face = axis * 2 + (sign > 0 ? 1 : 0);
      for (int u = 0; u < CHUNK_SIZE; ++u)
      {
        for (int v = 0; v < CHUNK_SIZE; ++v)
        {
          glm::ivec3 local(0);
          const int u_axis = (axis + 1) % 3;
          const int v_axis = (axis + 2) % 3;
          local[axis] = sign < 0 ? -1 : CHUNK_SIZE;
          local[u_axis] = u;
          local[v_axis] = v;
          const glm::ivec3 worldPos = origin + local;
          const int cell = u + v * CHUNK_SIZE;
          const int flat = ShellFlatIndex(face, cell);
          const glm::ivec3 lightChunkCoord =
              UChunkManager::WorldToChunk(worldPos);
          const UChunk *neighbor_chunk =
              world.GetChunkManager().GetChunk(lightChunkCoord);
          const bool neighbor_loaded = neighbor_chunk != nullptr;
          bool neighbor_visually_drawable = neighbor_loaded;
          if (neighbor_loaded && neighbor_drawable)
          {
            neighbor_visually_drawable =
                neighbor_drawable(neighbor_drawable_ctx, lightChunkCoord);
          }
          const BlockId raw = world.GetBlock(worldPos);
          snapshot.shellBlocks[static_cast<size_t>(flat)] =
              ShellBlockForNeighborOcclusion(raw, neighbor_visually_drawable);
          snapshot.shellNeighborState[static_cast<size_t>(flat)] =
              static_cast<uint8_t>(ClassifyShellCell(
                  neighbor_loaded,
                  snapshot.shellBlocks[static_cast<size_t>(flat)],
                  neighbor_visually_drawable));
          if (neighbor_chunk)
          {
            snapshot.shellLight[static_cast<size_t>(flat)] =
                neighbor_chunk->GetLightPackedLocal(
                    UChunkManager::WorldToLocal(worldPos));
          }
          snapshot.shellFluid[static_cast<size_t>(flat)] =
              PackFluidCellState(world.GetFluidState(worldPos));
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
  int face = 0;
  int cell = 0;
  if (TryShellIndex(local, face, cell))
  {
    return shellBlocks[static_cast<size_t>(ShellFlatIndex(face, cell))];
  }
  return BLOCK_AIR;
}

BlockId ChunkMeshSnapshot::GetBlockLocal(glm::ivec3 local) const
{
  return blocks[local.x + CHUNK_SIZE * local.y +
                CHUNK_SIZE * CHUNK_SIZE * local.z];
}

uint8_t ChunkMeshSnapshot::GetLightPackedLocal(glm::ivec3 local) const
{
  return light_packed[local.x + CHUNK_SIZE * local.y +
                      CHUNK_SIZE * CHUNK_SIZE * local.z];
}

uint8_t ChunkMeshSnapshot::GetLightPacked(glm::ivec3 worldPos) const
{
  const glm::ivec3 local = worldPos - ChunkOrigin();
  if (InChunkLocal(local))
  {
    return GetLightPackedLocal(local);
  }
  int face = 0;
  int cell = 0;
  if (TryShellIndex(local, face, cell))
  {
    return shellLight[static_cast<size_t>(ShellFlatIndex(face, cell))];
  }
  return 0;
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
  int face = 0;
  int cell = 0;
  if (TryShellIndex(local, face, cell))
  {
    return shellFluid[static_cast<size_t>(ShellFlatIndex(face, cell))];
  }
  return 0;
}

FluidCellState ChunkMeshSnapshot::GetFluid(glm::ivec3 worldPos) const
{
  return UnpackFluidCellState(GetFluidPacked(worldPos));
}

glm::ivec3 ChunkMeshSnapshot::ChunkOrigin() const { return coord * CHUNK_SIZE; }

NeighborLoadState ChunkMeshSnapshot::GetNeighborLoadState(
    glm::ivec3 worldPos) const
{
  const glm::ivec3 local = worldPos - ChunkOrigin();
  if (InChunkLocal(local))
  {
    return NeighborLoadState::Loaded;
  }
  int face = 0;
  int cell = 0;
  if (TryShellIndex(local, face, cell))
  {
    return static_cast<NeighborLoadState>(
        shellNeighborState[static_cast<size_t>(ShellFlatIndex(face, cell))]);
  }
  return NeighborLoadState::Unknown;
}

std::optional<ChunkMeshSnapshot> UBlockWorld::ReadChunkBandForCapture(
    glm::ivec3 coord, const MeshCaptureToken &token,
    ChunkMeshSnapshot::NeighborVisualDrawableFn neighbor_drawable,
    void *neighbor_drawable_ctx) const
{
  if (token.world_epoch == 0 || token.source_revision == 0)
  {
    return std::nullopt;
  }
  if (!GetChunkManager().GetChunk(coord))
  {
    return std::nullopt;
  }
  ChunkMeshSnapshot snap = ChunkMeshSnapshot::Capture(
      *this, coord, token.source_revision, neighbor_drawable,
      neighbor_drawable_ctx);
  snap.sourceRevision = token.source_revision;
  return snap;
}

} // namespace cutum
