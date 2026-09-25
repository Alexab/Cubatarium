#pragma once

#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkInputStamp.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
#include "Render/Mesh/MeshNeighborPolicy.h"
#include "Render/Mesh/BoundaryOverlay.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;

/// Why mesh apply was dropped as "stale visual" (legacy name): stamp/catalog
/// mismatch mid-flight — not SoftDefer drawable flip.
enum class MeshApplyStaleInputReason : uint8_t
{
  Ok = 0,
  StampInvalid = 1,
  Catalog = 2,
  Geom = 3,
  Light = 4,
};

/// Read-only voxel view for background meshing (center chunk + one-block
/// shell). Shell faces are dense arrays (6 * CHUNK_SIZE^2), not hash maps.
struct ChunkMeshSnapshot
{
  static constexpr int kShellFaceCount = 6;
  static constexpr int kShellFaceCells = CHUNK_SIZE * CHUNK_SIZE;
  static constexpr int kShellCells = kShellFaceCount * kShellFaceCells;
  static constexpr int kLightHaloRadius = 2;
  static constexpr int kLightHaloSize = CHUNK_SIZE + 2 * kLightHaloRadius;
  static constexpr int kLightHaloVolume =
      kLightHaloSize * kLightHaloSize * kLightHaloSize;

  glm::ivec3 coord{0};
  std::array<BlockId, CHUNK_VOLUME> blocks{};
  std::array<uint8_t, CHUNK_VOLUME> fluid_packed{};
  std::array<uint8_t, CHUNK_VOLUME> light_packed{};
  std::array<BlockId, kShellCells> shellBlocks{};
  std::array<uint8_t, kShellCells> shellFluid{};
  /// Light-only radius-2 padded neighborhood; covers FaceLightPacked fallback.
  std::array<uint8_t, kLightHaloVolume> paddedLight{};
  std::array<uint8_t, kShellCells> shellNeighborState{};
  uint64_t sourceRevision{0};
  std::array<ChunkInputStamp, kChunkMeshInputStampCount> inputStamps{};
  bool inputStampsValid{false};
  /// S4: versioned neighbor-missing overlay (not part of stamp equality).
  BoundaryOverlayState boundaryOverlay{};

  /// Optional: when false for a neighbor chunk coord, shell treats that
  /// neighbor as Air (Era39 SoftDefer-hidden seam). Nullptr ⇒ all drawable.
  /// Affects Capture shell only — never InputsStillValid / stamp equality.
  using NeighborVisualDrawableFn = bool (*)(void *ctx, glm::ivec3 neighbor_chunk);

  /// Geom/light stamp check. Optional drawable args are ignored (API symmetry
  /// with Capture call sites); SoftDefer visual flips do not invalidate.
  bool InputsStillValid(
      const UBlockWorld &world,
      NeighborVisualDrawableFn neighbor_drawable = nullptr,
      void *neighbor_drawable_ctx = nullptr) const;

  /// Post-N01 thrash autopsy: why InputsStillValid/catalog failed.
  /// Geom wins over Light if both halo stamps mismatch.
  static MeshApplyStaleInputReason ClassifyStaleInput(
      bool input_stamps_valid, bool catalog_match,
      const std::array<ChunkInputStamp, kChunkMeshInputStampCount> &stamps,
      const UBlockWorld &world);

  static ChunkMeshSnapshot Capture(const UBlockWorld &world,
                                   glm::ivec3 chunkCoord,
                                   uint64_t sourceRevision,
                                   NeighborVisualDrawableFn neighbor_drawable =
                                       nullptr,
                                   void *neighbor_drawable_ctx = nullptr);

  BlockId GetBlock(glm::ivec3 worldPos) const;
  /// Same as GetBlock but never forces AIR under boundary overlay (R06).
  BlockId GetBlockIgnoringOverlay(glm::ivec3 worldPos) const;
  BlockId GetBlockLocal(glm::ivec3 local) const;
  uint8_t GetLightPackedLocal(glm::ivec3 local) const;
  uint8_t GetLightPacked(glm::ivec3 worldPos) const;
  uint8_t GetFluidPackedLocal(glm::ivec3 local) const;
  uint8_t GetFluidPacked(glm::ivec3 worldPos) const;
  FluidCellState GetFluidLocal(glm::ivec3 local) const;
  FluidCellState GetFluid(glm::ivec3 worldPos) const;
  glm::ivec3 ChunkOrigin() const;
  NeighborLoadState GetNeighborLoadState(glm::ivec3 worldPos) const;
};

} // namespace cutum
