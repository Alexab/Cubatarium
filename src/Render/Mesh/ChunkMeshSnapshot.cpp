#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "App/Platform/Log.h"
#include "App/Settings/RenderSettings.h"
#include "Render/Mesh/BoundaryOverlay.h"
#include "Render/Mesh/MeshCaptureToken.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/LightUtil.h"
#include "World/Math/FluidCellState.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace cutum
{

SectionGeometryRenderInputs ChunkMeshSnapshot::GeometryRenderInputs(
    std::shared_ptr<const BlockDefinitionCatalog> catalog) const
{
  if (!inputStampsValid)
    return {};
  std::array<ChunkInputStamp, 7> geometry;
  std::copy_n(inputStamps.begin(), geometry.size(), geometry.begin());
  return SectionGeometryRenderInputs::FromStamps(coord, worldGeometryIdentity,
                                                 geometryProfile,
                                                 std::move(catalog), geometry);
}

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

// Fold only an axial second-plane coordinate onto the adjacent-plane index.
// Edge/corner and more distant reads remain unsupported, never wrapped.
bool TryOuterShellIndex(glm::ivec3 local, int &face, int &cell)
{
  bool outer = false;
  for (int axis = 0; axis < 3; ++axis)
  {
    if (local[axis] == -2)
    {
      local[axis] = -1;
      outer = true;
    }
    else if (local[axis] == CHUNK_SIZE + 1)
    {
      local[axis] = CHUNK_SIZE;
      outer = true;
    }
  }
  return outer && TryShellIndex(local, face, cell);
}

int ShellFlatIndex(int face, int cell)
{
  return face * ChunkMeshSnapshot::kShellFaceCells + cell;
}

int LightHaloFlatIndex(glm::ivec3 local)
{
  constexpr int pad = ChunkMeshSnapshot::kLightHaloRadius;
  constexpr int size = ChunkMeshSnapshot::kLightHaloSize;
  return (local.x + pad) + size * (local.y + pad) +
         size * size * (local.z + pad);
}

uint64_t PaddedLightHaloSignature(
    const std::array<uint8_t, ChunkMeshSnapshot::kLightHaloVolume> &padded,
    glm::ivec3 neighbor_offset, bool absent_open_sky)
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
  return LightHaloSignatureWithSkyEvidence(hash, absent_open_sky);
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
            ChunkMeshLightHaloSignature(
                neighbor, offset,
                !neighbor && ChunkMeshSnapshot::IsAbsentOpenSkyChunk(
                                 world, center_coord + offset)))
        {
          return false;
        }
      }
    }
  }
  return true;
}

bool MissingMeshEvidenceMatches(const ChunkInputStamp &stamp,
                                const UBlockWorld &world, const UChunk *chunk)
{
  if (stamp.missingEvidence == MissingChunkEvidence::Untracked)
    return true;
  if (chunk)
    return false;
  const bool known_air = world.IsKnownAbsentAirChunk(stamp.coord);
  return known_air == (stamp.missingEvidence == MissingChunkEvidence::KnownAir);
}

} // namespace

bool ChunkMeshSnapshot::IsAbsentOpenSkyChunk(const UBlockWorld &world,
                                             glm::ivec3 chunk_coord)
{
  const UChunkManager &chunks = world.GetChunkManager();
  if (chunks.GetChunk(chunk_coord))
  {
    return false;
  }
  const auto &profile = world.GetVerticalProfile();
  if (chunk_coord.y < profile.MinChunkY(CHUNK_SIZE))
    return false;
  if (chunk_coord.y > profile.MaxChunkY(CHUNK_SIZE))
    return true;
  return world.HasKnownOpenSkyAbsentProof(chunk_coord);
}

bool ChunkMeshSnapshot::IsKnownAirAbsentFaceNeighbor(const UBlockWorld &world,
                                                     glm::ivec3 neighbor_coord,
                                                     int axis, int sign)
{
  if (world.GetChunkManager().GetChunk(neighbor_coord))
  {
    return false;
  }
  (void)axis;
  (void)sign;
  return neighbor_coord.y < world.GetVerticalProfile().MinChunkY(CHUNK_SIZE) ||
         IsAbsentOpenSkyChunk(world, neighbor_coord);
}

bool ChunkMeshSnapshot::MeshInputStampMatchesWorld(const ChunkInputStamp &stamp,
                                                   const UBlockWorld &world)
{
  return world.InputStampStillMatches(stamp);
}

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
    if (!MeshInputStampMatchesWorld(stamp, world))
    {
      return false;
    }
  }
  if (certifiedPaddedLight)
    return static_cast<bool>(world.GetCurrentLightSamplingVolume(coord));
  return LightHaloSignaturesMatch(lightHaloSignatures, world, coord);
}

MeshApplyStaleInputReason ChunkMeshSnapshot::ClassifyStaleInput(
    bool input_stamps_valid, bool catalog_match,
    const std::array<ChunkInputStamp, kChunkMeshInputStampCount> &stamps,
    const UBlockWorld &world,
    const std::array<uint64_t, kChunkMeshNeighborStampCount> &light_signatures,
    bool certified_padded)
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
    const ChunkInputStamp current = ChunkInputStamp::Capture(
        stamp.coord, chunk, stamp.readsLight, stamp.readsContent);
    if (stamp.readsContent &&
        (stamp.incarnation != current.incarnation ||
         stamp.content != current.content ||
         !MissingMeshEvidenceMatches(stamp, world, chunk)))
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
  if (certified_padded
          ? !world.GetCurrentLightSamplingVolume(stamps[0].coord)
          : !LightHaloSignaturesMatch(light_signatures, world, stamps[0].coord))
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
  return CaptureImpl(world, chunkCoord, sourceRevision, neighbor_drawable,
                     neighbor_drawable_ctx, true);
}

ChunkMeshSnapshot ChunkMeshSnapshot::CaptureGeometry(const UBlockWorld &world,
                                                     glm::ivec3 coord)
{
  return CaptureImpl(world, coord, 0, nullptr, nullptr, false);
}

ChunkMeshSnapshot
ChunkMeshSnapshot::CaptureImpl(const UBlockWorld &world, glm::ivec3 chunkCoord,
                               uint64_t sourceRevision,
                               NeighborVisualDrawableFn neighbor_drawable,
                               void *neighbor_drawable_ctx, bool capture_light)
{
  const auto capture_t0 = std::chrono::steady_clock::now();
  auto stage_t0 = capture_t0;
  const auto note_slow_stage = [&](const char *stage)
  {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(now - stage_t0).count();
    constexpr double kSlowCaptureLogThresholdMs = 25.0;
    if (elapsed_ms >= kSlowCaptureLogThresholdMs)
    {
      std::ostringstream message;
      message << "stage=" << stage << " coord=(" << chunkCoord.x << ","
              << chunkCoord.y << "," << chunkCoord.z
              << ") elapsed_ms=" << elapsed_ms;
      CubatariumLogInfo("MeshCaptureSlow", message.str());
    }
    stage_t0 = now;
  };

  ChunkMeshSnapshot snapshot;
  snapshot.coord = chunkCoord;
  snapshot.worldGeometryIdentity =
      world.GetChunkManager().GetGeometryInputRevisionIdentity();
  snapshot.geometryProfile = world.GetVerticalProfile();
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
  if (capture_light)
    snapshot.light_packed = chunk->GetLightData();
  note_slow_stage("center_chunk_copy");
  if (const char *trace_env = []() -> const char *
      {
        static const char *const v = std::getenv("CUBA_TRACE_CHUNK");
        return v;
      }())
  {
    int tx = 0, ty = 0, tz = 0;
    if (std::sscanf(trace_env, "%d,%d,%d", &tx, &ty, &tz) == 3 &&
        chunkCoord == glm::ivec3(tx, ty, tz))
    {
      std::ostringstream message;
      message << "capture coord=(" << tx << "," << ty << "," << tz
              << ") content=" << chunk->GetContentRevision()
              << " incarnation=" << chunk->GetIncarnation()
              << " light=" << chunk->GetLightFieldRevision()
              << " nonair=" << chunk->GetNonAirCount()
              << " b(8,3,8)=" << chunk->GetBlockLocal({8, 3, 8})
              << " b(8,4,8)=" << chunk->GetBlockLocal({8, 4, 8})
              << " sourceRev=" << sourceRevision;
      CubatariumLogInfo("ChunkTrace", message.str());
    }
  }

  const glm::ivec3 origin = snapshot.ChunkOrigin();
  // Capture exactly the radius-2 light neighborhood read by FaceLightPacked.
  // Copy by chunk once (27 lookups), not by voxel through the chunk map.
  if (capture_light)
  {
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
          const bool absent_open_sky =
              (dx != 0 || dy != 0 || dz != 0) && !light_chunk &&
              IsAbsentOpenSkyChunk(world, chunkCoord + chunk_offset);
          if (absent_open_sky)
          {
            snapshot.openSkyAbsentMask |=
                1u << ((dy + 1) * 9 + (dz + 1) * 3 + (dx + 1));
          }
          if (dx != 0 || dy != 0 || dz != 0)
          {
            snapshot.lightHaloSignatures[signature_index++] =
                PaddedLightHaloSignature(snapshot.paddedLight, chunk_offset,
                                         absent_open_sky);
          }
        }
      }
    }
    snapshot.renderLight = world.CaptureRenderLightSnapshot(chunkCoord);
    const auto captured_volume =
        snapshot.renderLight ? snapshot.renderLight->Volume()
                             : world.GetCurrentLightSamplingVolume(chunkCoord);
    if (const auto &volume = captured_volume)
    {
      static_assert(SectionLightVolume::Width == kLightHaloSize);
      for (int z = -kLightHaloRadius; z < CHUNK_SIZE + kLightHaloRadius; ++z)
        for (int y = -kLightHaloRadius; y < CHUNK_SIZE + kLightHaloRadius; ++y)
          for (int x = -kLightHaloRadius; x < CHUNK_SIZE + kLightHaloRadius;
               ++x)
            snapshot.paddedLight[LightHaloFlatIndex({x, y, z})] =
                volume->cells[SectionLightVolume::Index(x, y, z)];
      snapshot.openSkyAbsentMask = 0;
      snapshot.certifiedPaddedLight = true;
    }
    snapshot.provisionalLightPreview =
        !snapshot.certifiedPaddedLight &&
        world.GetLightingConfigurationIdentity() !=
            static_cast<uint64_t>(LightingMode::Flat);
    note_slow_stage("light_halo_capture");
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
      // A missing chunk below the configured world floor is known exterior,
      // while missing slices inside the world remain Unknown until streamed.
      const bool known_world_exterior =
          !neighbor_loaded &&
          IsKnownAirAbsentFaceNeighbor(world, neighbor_coord, axis, sign);
      // Stamp = geom/light only. Drawable fn affects shell occlusion preview.
      snapshot.inputStamps[static_cast<size_t>(face + 1)] =
          ChunkInputStamp::Capture(neighbor_coord, neighbor_chunk,
                                   /*reads_light=*/false,
                                   /*reads_content=*/true);
      if (!neighbor_loaded)
        snapshot.inputStamps[static_cast<size_t>(face + 1)].missingEvidence =
            known_world_exterior ? MissingChunkEvidence::KnownAir
                                 : MissingChunkEvidence::Unknown;
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
          auto outer_local = local;
          outer_local[axis] += sign;
          snapshot.outerShellBlocks[static_cast<size_t>(flat)] =
              neighbor_chunk
                  ? neighbor_chunk->GetBlockLocal(
                        UChunkManager::WorldToLocal(origin + outer_local))
                  : world.GetBlock(origin + outer_local);

          snapshot.shellNeighborState[static_cast<size_t>(flat)] =
              static_cast<uint8_t>(
                  known_world_exterior
                      ? NeighborLoadState::Air
                      : ClassifyShellCell(neighbor_loaded, raw,
                                          neighbor_visually_drawable));
          // The face neighbor was looked up once above. Read its packed fluid
          // byte directly instead of repeating ChunkManager's hash lookup for
          // every shell voxel (6 * CHUNK_SIZE * CHUNK_SIZE lookups per capture).
          // An unloaded neighbor has no chunk-owned fluid, matching
          // UChunkManager::GetFluidState's empty result.
          snapshot.shellFluid[static_cast<size_t>(flat)] =
              neighbor_chunk
                  ? neighbor_chunk->GetFluidData()[static_cast<size_t>(
                        UChunk::LocalIndex(
                            UChunkManager::WorldToLocal(worldPos)))]
                  : uint8_t{0};
        }
      }
      // Ownership SeamVisibility: overlay Missing only for true unload.
      // SoftDefer / !drawable → ClassifyShellCell Unlit (emit faces); do NOT
      // force Unknown via overlay (sky-through SoT 161139).
      if (!neighbor_loaded && !known_world_exterior)
        missing_faces = static_cast<uint8_t>(missing_faces | (1u << face));
    }
  }
  note_slow_stage("face_shell_capture");
  if (!capture_light)
  {
    BoundaryOverlaySetMissingFaces(snapshot.boundaryOverlay, missing_faces);
    return snapshot;
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
  note_slow_stage("diagonal_stamps_and_overlay");
  const double total_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - capture_t0)
                              .count();
  if (total_ms >= 25.0)
  {
    std::ostringstream message;
    message << "stage=snapshot_total coord=(" << chunkCoord.x << ","
            << chunkCoord.y << "," << chunkCoord.z
            << ") elapsed_ms=" << total_ms;
    CubatariumLogInfo("MeshCaptureSlow", message.str());
  }
  return snapshot;
}

BlockId ChunkMeshSnapshot::GetBlock(glm::ivec3 worldPos) const
{
  const glm::ivec3 local = worldPos - ChunkOrigin();
  if (InChunkLocal(local))
    return GetBlockLocal(local);
  int face = 0;
  int cell = 0;
  bool outer = false;
  if (!TryShellIndex(local, face, cell))
  {
    outer = TryOuterShellIndex(local, face, cell);
    if (!outer)
      return BLOCK_AIR;
  }
  if (boundaryOverlay.active &&
      (boundaryOverlay.missingNeighborFaces & static_cast<uint8_t>(1u << face)))
    return BLOCK_AIR;
  const auto index = static_cast<size_t>(ShellFlatIndex(face, cell));
  return outer ? outerShellBlocks[index] : shellBlocks[index];
}

BlockId ChunkMeshSnapshot::GetBlockIgnoringOverlay(glm::ivec3 worldPos) const
{
  const glm::ivec3 local = worldPos - ChunkOrigin();
  if (InChunkLocal(local))
    return GetBlockLocal(local);
  int face = 0;
  int cell = 0;
  if (TryShellIndex(local, face, cell))
    return shellBlocks[static_cast<size_t>(ShellFlatIndex(face, cell))];
  if (TryOuterShellIndex(local, face, cell))
    return outerShellBlocks[static_cast<size_t>(ShellFlatIndex(face, cell))];
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
    if (openSkyAbsentMask != 0)
    {
      const int dx = local.x < 0 ? -1 : (local.x >= CHUNK_SIZE ? 1 : 0);
      const int dy = local.y < 0 ? -1 : (local.y >= CHUNK_SIZE ? 1 : 0);
      const int dz = local.z < 0 ? -1 : (local.z >= CHUNK_SIZE ? 1 : 0);
      if ((openSkyAbsentMask &
           (1u << ((dy + 1) * 9 + (dz + 1) * 3 + (dx + 1)))) != 0)
      {
        return PackLight(kMaxLightLevel, 0);
      }
    }
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
      // A missing +Y chunk is often an air-only slice that terrain streaming
      // deliberately never materializes. Emit the local top surface during
      // bootstrap, but keep BoundaryOverlay/FaceDebt intact so a later
      // resident peer can trigger the normal seam-remesh and remove any
      // provisional faces. Unknown side and bottom faces still stay hidden to
      // avoid the distant closing walls this overlay was introduced to stop.
      if (IsProvisionalMissingTopFace(face))
      {
        return NeighborLoadState::Air;
      }
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
