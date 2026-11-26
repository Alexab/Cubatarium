#ifndef CHUNK_H
#define CHUNK_H

#include "World/Lighting/SectionLightField.h"
#include "Core/Memory/ScopedSharedAllocation.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>

namespace cutum
{

class UChunk
{
public:
  static constexpr int kGeometryBoundaryDepth = 2;
  explicit UChunk(glm::ivec3 chunkCoord);
  UChunk(const UChunk &) = default;
  /// A managed destination keeps its coordinate/clock and starts a fresh
  /// identity.
  UChunk &operator=(const UChunk &other);

  glm::ivec3 GetCoord() const { return Coord; }
  BlockId GetBlockLocal(glm::ivec3 local) const;
  void SetBlockLocal(glm::ivec3 local, BlockId Id);
  FluidCellState GetFluidLocal(glm::ivec3 local) const;
  void SetFluidLocal(glm::ivec3 local, FluidCellState state);
  void ClearFluidLocal(glm::ivec3 local);
  bool IsDirty() const { return Dirty; }
  void ClearDirty() { Dirty = false; }
  void MarkDirty() { Dirty = true; }
  /// Recycle for free-list: clear voxels/light/fluid and rebind coord.
  void ResetForReuse(glm::ivec3 chunkCoord);

  const std::array<BlockId, CHUNK_VOLUME> &GetData() const { return Data; }
  const std::array<uint8_t, CHUNK_VOLUME> &GetFluidData() const
  {
    return FluidData;
  }

  /// O(1) air-only query — maintained on SetBlockLocal / ResetForReuse.
  bool IsAirOnly() const { return NonAirCount == 0; }
  int GetNonAirCount() const { return NonAirCount; }

  uint8_t GetLightPackedLocal(glm::ivec3 local) const;
  int GetSkyLightLocal(glm::ivec3 local) const;
  int GetBlockLightLocal(glm::ivec3 local) const;
  void SetLightLocal(glm::ivec3 local, int sky_level, int block_level);
  void ClearLightLocal(glm::ivec3 local);
  float GetSurfaceWetness() const { return SurfaceWetness; }
  double GetWeatherLastUpdateSec() const { return WeatherLastUpdateSec; }
  void SetWeatherState(float wetness, double last_update_sec);

  const std::array<uint8_t, CHUNK_VOLUME> &GetLightData() const
  {
    return Light.field->LightData;
  }
  /// Atomic main-thread replacement. Unchanged channels retain their identity.
  bool ReplaceLightData(const std::array<uint8_t, CHUNK_VOLUME> &data);
  bool ReplaceComputedLightVolume(const SectionLightVolume &volume, bool sky,
                                  bool block);
  /// FZ2.7-B1: bumped on any light-field mutation (Apply merge, relight seed).
  const SectionLightField &GetSectionLightField() const { return *Light.field; }
  uint64_t GetLightFieldRevision() const
  {
    return Light.field->LightFieldRevision;
  }
  uint64_t GetSkyLightRevision() const { return Light.field->SkyLightRevision; }
  uint64_t GetBlockLightRevision() const
  {
    return Light.field->BlockLightRevision;
  }
  uint64_t GetIncarnation() const { return Incarnation; }
  uint64_t GetContentRevision() const { return ContentRevision; }
  const std::array<uint64_t, 6> &GetBoundaryContentRevisions() const
  {
    return BoundaryContentRevisions;
  }
  void BumpLightFieldRevision()
  {
    ++Light.field->LightFieldRevision;
    InputRevision.NoteMutation();
  }

  /// Main-thread calculation provenance is owned by this resident generation.
  void ClearLightCalculationProofs() { Light.field->LightProofs = {}; }

  static int LocalIndex(glm::ivec3 local);

  /// Memo of ChunkMeshLightHaloSignature per neighbor offset (index
  /// (dy+1)*9+(dz+1)*3+(dx+1)), valid for one (incarnation, light revision).
  /// Hashing the halo cells on every stamp validation / capture showed up as
  /// the top self cost of the streaming tick.
  bool TryGetCachedHaloSignature(int index, uint64_t &out) const
  {
    std::lock_guard<std::mutex> lock(Light.field->HaloSigCache.Mutex);
    if (Light.field->HaloSigCache.Valid[index] &&
        Light.field->HaloSigCache.Incarnation[index] == Incarnation &&
        Light.field->HaloSigCache.LightRev[index] ==
            Light.field->LightFieldRevision)
    {
      out = Light.field->HaloSigCache.Sig[index];
      return true;
    }
    return false;
  }
  void StoreCachedHaloSignature(int index, uint64_t value) const
  {
    std::lock_guard<std::mutex> lock(Light.field->HaloSigCache.Mutex);
    Light.field->HaloSigCache.Valid[index] = true;
    Light.field->HaloSigCache.Incarnation[index] = Incarnation;
    Light.field->HaloSigCache.LightRev[index] = Light.field->LightFieldRevision;
    Light.field->HaloSigCache.Sig[index] = value;
  }

private:
  friend class AllocatorOwnerCensus;
  friend class UBlockWorld;
  friend class UChunkManager;
  /// Copies of voxel data do not acquire ownership of the source world's
  /// clock. Assignment preserves the destination binding and invalidates it.
  struct InputRevisionBinding
  {
    std::shared_ptr<uint64_t> clock;
    std::shared_ptr<uint64_t> geometry_clock;
    InputRevisionBinding() = default;
    InputRevisionBinding(const InputRevisionBinding &) {}
    InputRevisionBinding &operator=(const InputRevisionBinding &)
    {
      NoteGeometryMutation();
      return *this;
    }
    void NoteMutation() const
    {
      if (clock)
        ++*clock;
    }
    void NoteGeometryMutation() const
    {
      NoteMutation();
      if (geometry_clock)
        ++*geometry_clock;
    }
  };
  InputRevisionBinding InputRevision;
  struct LightFieldBinding
  {
    std::shared_ptr<SectionLightField> field{
        MakeScopedShared<SectionLightField>()};
    LightFieldBinding() = default;
    LightFieldBinding(const LightFieldBinding &other)
        : field(MakeScopedShared<SectionLightField>(*other.field))
    {
    }
    LightFieldBinding &operator=(const LightFieldBinding &other)
    {
      *field = *other.field;
      return *this;
    }
  };
  LightFieldBinding Light;
  glm::ivec3 Coord;
  std::array<BlockId, CHUNK_VOLUME> Data{};
  std::array<uint8_t, CHUNK_VOLUME> FluidData{};
  float SurfaceWetness{0.0f};
  double WeatherLastUpdateSec{0.0};
  inline static std::atomic<uint64_t> NextIncarnation{1};
  uint64_t Incarnation{NextIncarnation.fetch_add(1, std::memory_order_relaxed)};
  uint64_t ContentRevision{0};
  // Runtime-only clocks for the six voxel/fluid boundary slabs (-X,+X,...).
  // Incarnation still protects replacement/reuse; own geometry uses full
  // content.
  std::array<uint64_t, 6> BoundaryContentRevisions{};
  void NoteContentMutation(glm::ivec3 local);
  bool Dirty{true};
  /// Count of non-air blocks; IsAirOnly() when zero (avoids 4096-voxel scans).
  int NonAirCount{0};
};

} // namespace cutum

#endif
