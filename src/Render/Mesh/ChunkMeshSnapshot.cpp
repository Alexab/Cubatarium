#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/BoundaryOverlay.h"
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

bool ChunkMeshSnapshot::InputsStillValid(
    const UBlockWorld &world, NeighborVisualDrawableFn /*neighbor_drawable*/,
    void * /*neighbor_drawable_ctx*/) const
{
  // Geom/light/catalog identity only. NeighborDrawableFn may be passed for
  // call-site symmetry with Capture, but must never affect stamp validity
  // (Strategy A Phase1 — SoftDefer visual flip must not remesh).
  if (!inputStampsValid)
  {
    return false;
  }
  for (const auto &stamp : inputStamps)
  {
    if (!stamp.Matches(world.GetChunkManager().GetChunk(stamp.coord)))
    {
      return false;
    }
  }
  return true;
}

MeshApplyStaleInputReason ChunkMeshSnapshot::ClassifyStaleInput(
    bool input_stamps_valid, bool catalog_match,
    const std::array<ChunkInputStamp, 7> &stamps, const UBlockWorld &world)
{
  if (!input_stamps_valid)
    return MeshApplyStaleInputReason::StampInvalid;
  if (!catalog_match)
    return MeshApplyStaleInputReason::Catalog;
  bool any_geom = false;
  bool any_light = false;
  for (const auto &stamp : stamps)
  {
    const UChunk *chunk = world.GetChunkManager().GetChunk(stamp.coord);
    const ChunkInputStamp current =
        ChunkInputStamp::Capture(stamp.coord, chunk, stamp.readsLight);
    if (stamp.incarnation != current.incarnation ||
        stamp.content != current.content)
    {
      any_geom = true;
      continue;
    }
    if (stamp.readsLight && stamp.light != current.light)
      any_light = true;
  }
  if (any_geom)
    return MeshApplyStaleInputReason::Geom;
  if (any_light)
    return MeshApplyStaleInputReason::Light;
  return MeshApplyStaleInputReason::Ok;
}

ChunkMeshSnapshot ChunkMeshSnapshot::Capture(
    const UBlockWorld &world, glm::ivec3 chunkCoord, uint64_t sourceRevision,
    NeighborVisualDrawableFn neighbor_drawable, void *neighbor_drawable_ctx)
{
  ChunkMeshSnapshot snapshot;
  snapshot.coord = chunkCoord;
  snapshot.sourceRevision = sourceRevision;
  const UChunk *chunk = world.GetChunkManager().GetChunk(chunkCoord);
  snapshot.inputStamps[0] = ChunkInputStamp::Capture(chunkCoord, chunk);
  if (!chunk)
  {
    return snapshot;
  }
  snapshot.blocks = chunk->GetData();
  snapshot.inputStampsValid = true;
  snapshot.fluid_packed = chunk->GetFluidData();
  snapshot.light_packed = chunk->GetLightData();

  const glm::ivec3 origin = snapshot.ChunkOrigin();
  uint8_t missing_faces = 0;
  for (int axis = 0; axis < 3; ++axis)
  {
    for (int sign = -1; sign <= 1; sign += 2)
    {
      const int face = axis * 2 + (sign > 0 ? 1 : 0);
      glm::ivec3 neighbor_coord = chunkCoord;
      neighbor_coord[axis] += sign;
      const UChunk *neighbor_chunk =
          world.GetChunkManager().GetChunk(neighbor_coord);
      const bool neighbor_loaded = neighbor_chunk != nullptr;
      // Stamp = geom/light only. Drawable fn affects shell occlusion preview.
      snapshot.inputStamps[static_cast<size_t>(face + 1)] =
          ChunkInputStamp::Capture(neighbor_coord, neighbor_chunk);
      bool neighbor_visually_drawable = neighbor_loaded;
      if (neighbor_loaded && neighbor_drawable)
      {
        neighbor_visually_drawable =
            neighbor_drawable(neighbor_drawable_ctx, neighbor_coord);
      }
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
          BlockId raw = BLOCK_AIR;
          if (neighbor_chunk)
          {
            raw = neighbor_chunk->GetBlockLocal(
                UChunkManager::WorldToLocal(worldPos));
          }
          else
          {
            raw = world.GetBlock(worldPos);
          }
          snapshot.shellBlocks[static_cast<size_t>(flat)] = raw;
          snapshot.shellNeighborState[static_cast<size_t>(flat)] =
              static_cast<uint8_t>(ClassifyShellCell(
                  neighbor_loaded, raw, neighbor_visually_drawable));
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
      // S4 overlay: missing published coverage (unloaded OR not drawable).
      // Shell keeps raw voxels; mesher emits closing via overlay-aware load/GetBlock.
      if (!neighbor_loaded || !neighbor_visually_drawable)
        missing_faces = static_cast<uint8_t>(missing_faces | (1u << face));
    }
  }
  BoundaryOverlaySetMissingFaces(snapshot.boundaryOverlay, missing_faces);
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
    // Overlay closing: treat missing published coverage as open air for hide.
    if (boundaryOverlay.active &&
        (boundaryOverlay.missingNeighborFaces &
         static_cast<uint8_t>(1u << face)) != 0)
    {
      return BLOCK_AIR;
    }
    return shellBlocks[static_cast<size_t>(ShellFlatIndex(face, cell))];
  }
  return BLOCK_AIR;
}

BlockId ChunkMeshSnapshot::GetBlockIgnoringOverlay(glm::ivec3 worldPos) const
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
    // Overlay force-emit path: Air (not Unknown) so solids emit closing faces.
    if (boundaryOverlay.active &&
        (boundaryOverlay.missingNeighborFaces &
         static_cast<uint8_t>(1u << face)) != 0)
    {
      return NeighborLoadState::Air;
    }
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
