#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/BoundaryOverlay.h"
#include "Render/Mesh/MeshCaptureToken.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include <cstdlib>

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

int LightHaloFlatIndex(glm::ivec3 local)
{
  constexpr int pad = ChunkMeshSnapshot::kLightHaloRadius;
  constexpr int size = ChunkMeshSnapshot::kLightHaloSize;
  return (local.x + pad) + size * (local.y + pad) + size * size * (local.z + pad);
}

uint64_t PaddedLightHaloSignature(
    const std::array<uint8_t, ChunkMeshSnapshot::kLightHaloVolume> &padded,
    glm::ivec3 neighbor_offset)
{
  constexpr uint64_t kFnvOffset = 1469598103934665603ull;
  constexpr uint64_t kFnvPrime = 1099511628211ull;
  glm::ivec3 local_min(0);
  glm::ivec3 local_max(CHUNK_SIZE - 1);
  for (int axis = 0; axis < 3; ++axis)
  {
    if (neighbor_offset[axis] < 0)
    {
      local_min[axis] = -ChunkMeshSnapshot::kLightHaloRadius;
      local_max[axis] = -1;
    }
    else if (neighbor_offset[axis] > 0)
    {
      local_min[axis] = CHUNK_SIZE;
      local_max[axis] = CHUNK_SIZE + ChunkMeshSnapshot::kLightHaloRadius - 1;
    }
  }
  uint64_t hash = kFnvOffset;
  for (int y = local_min.y; y <= local_max.y; ++y)
  {
    for (int z = local_min.z; z <= local_max.z; ++z)
    {
      for (int x = local_min.x; x <= local_max.x; ++x)
      {
        hash ^= padded[static_cast<size_t>(LightHaloFlatIndex({x, y, z}))];
        hash *= kFnvPrime;
      }
    }
  }
  return hash;
}

bool LightHaloSignaturesMatch(
    const std::array<uint64_t, kChunkMeshNeighborStampCount> &signatures,
    const UBlockWorld &world, glm::ivec3 center_coord)
{
  size_t signature_index = 0;
  for (int dy = -1; dy <= 1; ++dy)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int dx = -1; dx <= 1; ++dx)
      {
        if (dx == 0 && dy == 0 && dz == 0)
        {
          continue;
        }
        const glm::ivec3 offset(dx, dy, dz);
        const UChunk *neighbor =
            world.GetChunkManager().GetChunk(center_coord + offset);
        if (signatures[signature_index++] !=
            ChunkMeshLightHaloSignature(neighbor, offset))
        {
          return false;
        }
      }
    }
  }
  return true;
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
  return LightHaloSignaturesMatch(lightHaloSignatures, world, coord);
}

MeshApplyStaleInputReason ChunkMeshSnapshot::ClassifyStaleInput(
    bool input_stamps_valid, bool catalog_match,
    const std::array<ChunkInputStamp, kChunkMeshInputStampCount> &stamps,
    const UBlockWorld &world,
    const std::array<uint64_t, kChunkMeshNeighborStampCount> &light_signatures)
{
  if (!input_stamps_valid)
    return MeshApplyStaleInputReason::StampInvalid;
  if (!catalog_match)
    return MeshApplyStaleInputReason::Catalog;
  bool any_geom = false;
  bool any_light = false;
  for (size_t i = 0; i < stamps.size(); ++i)
  {
    const auto &stamp = stamps[i];
    if (!stamp.readsContent && !stamp.readsLight)
    {
      continue;
    }
    const UChunk *chunk = world.GetChunkManager().GetChunk(stamp.coord);
    const ChunkInputStamp current =
        ChunkInputStamp::Capture(stamp.coord, chunk, stamp.readsLight,
                                 stamp.readsContent);
    if (stamp.readsContent &&
        (stamp.incarnation != current.incarnation ||
         stamp.content != current.content))
    {
      any_geom = true;
    }
    // Only center light is sampled throughout the interior. Neighbor light
    // is validated below from exact halo cells, not whole-chunk revisions.
    if (i == 0 && stamp.readsLight && stamp.light != current.light)
    {
      any_light = true;
    }
  }
  if (!LightHaloSignaturesMatch(light_signatures, world, stamps[0].coord))
  {
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
  // Capture exactly the radius-2 light neighborhood read by FaceLightPacked.
  // Copy by chunk once (27 lookups), not by voxel through the chunk map.
  size_t signature_index = 0;
  for (int dy = -1; dy <= 1; ++dy)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int dx = -1; dx <= 1; ++dx)
      {
        const glm::ivec3 chunk_offset(dx, dy, dz);
        const UChunk *light_chunk =
            world.GetChunkManager().GetChunk(chunkCoord + chunk_offset);
        if (light_chunk && (dx != 0 || dy != 0 || dz != 0))
        {
          glm::ivec3 dst_min(0);
          glm::ivec3 dst_max(CHUNK_SIZE - 1);
          for (int axis = 0; axis < 3; ++axis)
          {
            if (chunk_offset[axis] < 0)
            {
              dst_min[axis] = -kLightHaloRadius;
              dst_max[axis] = -1;
            }
            else if (chunk_offset[axis] > 0)
            {
              dst_min[axis] = CHUNK_SIZE;
              dst_max[axis] = CHUNK_SIZE + kLightHaloRadius - 1;
            }
          }
          for (int y = dst_min.y; y <= dst_max.y; ++y)
          {
            for (int z = dst_min.z; z <= dst_max.z; ++z)
            {
              for (int x = dst_min.x; x <= dst_max.x; ++x)
              {
                const glm::ivec3 dst_local(x, y, z);
                const glm::ivec3 neighbor_local =
                    dst_local - chunk_offset * CHUNK_SIZE;
                snapshot.paddedLight[static_cast<size_t>(
                    LightHaloFlatIndex(dst_local))] =
                    light_chunk->GetLightPackedLocal(neighbor_local);
              }
            }
          }
        }
        if (dx != 0 || dy != 0 || dz != 0)
        {
          snapshot.lightHaloSignatures[signature_index++] =
              PaddedLightHaloSignature(snapshot.paddedLight, chunk_offset);
        }
      }
    }
  }

  // Keep center + six face stamps at stable indices for existing consumers.
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
          ChunkInputStamp::Capture(neighbor_coord, neighbor_chunk,
                                   /*reads_light=*/false,
                                   /*reads_content=*/true);
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
          snapshot.shellFluid[static_cast<size_t>(flat)] =
              PackFluidCellState(world.GetFluidState(worldPos));
        }
      }
      // Ownership SeamVisibility: overlay Missing only for true unload.
      // SoftDefer / !drawable → ClassifyShellCell Unlit (emit faces); do NOT
      // force Unknown via overlay (sky-through SoT 161139).
      if (!neighbor_loaded)
        missing_faces = static_cast<uint8_t>(missing_faces | (1u << face));
    }
  }
  size_t stamp_index = 7;
  for (int dy = -1; dy <= 1; ++dy)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int dx = -1; dx <= 1; ++dx)
      {
        if (std::abs(dx) + std::abs(dy) + std::abs(dz) <= 1)
        {
          continue;
        }
        const glm::ivec3 neighbor_coord =
            chunkCoord + glm::ivec3(dx, dy, dz);
        // Diagonal chunks can contribute only to the radius-2 lighting
        // fallback; block geometry reads remain center + six face shells.
        snapshot.inputStamps[stamp_index++] = ChunkInputStamp::Capture(
            neighbor_coord,
            world.GetChunkManager().GetChunk(neighbor_coord), false, false);
      }
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
  constexpr int kMin = -kLightHaloRadius;
  constexpr int kMax = CHUNK_SIZE + kLightHaloRadius - 1;
  if (local.x >= kMin && local.x <= kMax && local.y >= kMin &&
      local.y <= kMax && local.z >= kMin && local.z <= kMax)
  {
    return paddedLight[static_cast<size_t>(LightHaloFlatIndex(local))];
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
    // Overlay missing-neighbor: Unknown only for true unload (Missing bit set
    // only when neighbor chunk absent — SoftDefer uses Unlit shell state).
    // Liquid void emit is gated separately in NeighborHidesFace.
    if (boundaryOverlay.active &&
        (boundaryOverlay.missingNeighborFaces &
         static_cast<uint8_t>(1u << face)) != 0)
    {
      return NeighborLoadState::Unknown;
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
