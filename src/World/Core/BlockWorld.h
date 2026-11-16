#ifndef BLOCKWORLD_H
#define BLOCKWORLD_H

#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/MeshCaptureToken.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
#include "World/Math/WorldVerticalProfile.h"
#include <functional>
#include <glm/glm.hpp>
#include <optional>

namespace cutum
{

class UBlockDefinitionStorage;
class UChunkBuffer;

class UBlockWorld
{
public:
  /// Runtime snapshot of the current world's vertical coordinate profile.
  void SetVerticalProfile(WorldVerticalProfile profile)
  {
    VerticalProfile = profile;
  }
  const WorldVerticalProfile &GetVerticalProfile() const
  {
    return VerticalProfile;
  }

  /// Main-thread capture query supplied by the owning world's terrain/IO
  /// completion evidence. A lower resident chunk alone is not proof of sky.
  void SetKnownOpenSkyAbsentQuery(std::function<bool(glm::ivec3)> query)
  {
    KnownOpenSkyAbsentQuery = std::move(query);
  }
  bool HasKnownOpenSkyAbsentProof(glm::ivec3 coord) const
  {
    return KnownOpenSkyAbsentQuery && KnownOpenSkyAbsentQuery(coord);
  }

  /// Main-thread dependency identity includes the evidence for absent data.
  bool IsKnownAbsentAirChunk(glm::ivec3 coord) const;
  ChunkInputStamp CaptureInputStamp(glm::ivec3 coord, bool reads_light = true,
                                    bool reads_content = true) const;
  bool InputStampStillMatches(const ChunkInputStamp &stamp) const;
  bool InputStampLightDomainsStillMatch(const ChunkInputStamp &stamp,
                                        bool reads_sky, bool reads_block) const;
  void SetLightingConfigurationIdentity(uint64_t identity)
  {
    LightingConfigurationIdentity = identity;
  }
  uint64_t GetLightingConfigurationIdentity() const
  {
    return LightingConfigurationIdentity;
  }

  void SetFluidDefinitions(const UBlockDefinitionStorage *definitions)
  {
    FluidDefinitions = definitions;
  }

  /// Optional gen-scratch mirror: every SetBlock/SetFluid also updates this
  /// buffer (worker populate → ApplyTo live world without a final world scan).
  void SetCaptureBuffer(UChunkBuffer *buffer) { CaptureBuffer = buffer; }
  UChunkBuffer *GetCaptureBuffer() const { return CaptureBuffer; }

  BlockId GetBlock(glm::ivec3 pos) const;
  BlockQueryResult QueryBlock(glm::ivec3 pos) const;
  FluidCellState GetFluidState(glm::ivec3 pos) const;
  void SetBlock(glm::ivec3 pos, BlockId Id);
  /// Apply a block when the owning chunk is already known by the caller.
  /// This avoids repeating the chunk-coordinate hash lookup for bulk chunk IO.
  void SetBlockInChunk(UChunk &chunk, glm::ivec3 pos, BlockId Id);
  void SetFluidState(glm::ivec3 pos, FluidCellState state);
  void SetFluidStateInChunk(UChunk &chunk, glm::ivec3 pos,
                            FluidCellState state);
  void ClearFluidState(glm::ivec3 pos);
  bool IsAir(glm::ivec3 pos) const;
  void Clear();
  size_t CountNonAir() const;
  /// Early-out existence check (UX U2/S6): the coop load path only needs
  /// "is the world empty" - a full scan used to walk every block.
  bool HasAnyNonAir() const;
  void ForEachBlock(const std::function<void(glm::ivec3, BlockId)> &fn) const;

  UChunkManager &GetChunkManager() { return Chunks; }
  const UChunkManager &GetChunkManager() const { return Chunks; }

  /// Main-thread immutable band for worker / store (§A.4).
  std::optional<ChunkMeshSnapshot> ReadChunkBandForCapture(
      glm::ivec3 coord, const MeshCaptureToken &token,
      ChunkMeshSnapshot::NeighborVisualDrawableFn neighbor_drawable = nullptr,
      void *neighbor_drawable_ctx = nullptr) const;

private:
  UChunkManager Chunks;
  WorldVerticalProfile VerticalProfile{};
  uint64_t LightingConfigurationIdentity{0};
  const UBlockDefinitionStorage *FluidDefinitions{nullptr};
  UChunkBuffer *CaptureBuffer{nullptr};
  std::function<bool(glm::ivec3)> KnownOpenSkyAbsentQuery;
};

} // namespace cutum

#endif
