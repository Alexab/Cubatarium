#ifndef CHUNK_H
#define CHUNK_H

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

constexpr int CHUNK_SIZE = 16;
constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;

struct RelightComputeResult;
class UBlockWorld;

enum class LightDomain
{
  Sky,
  Block
};
enum class LightCalculationState
{
  Missing,
  Transient,
  Complete
};

class UChunk
{
public:
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
    return LightData;
  }
  /// Atomic main-thread replacement. Unchanged channels retain their identity.
  bool ReplaceLightData(const std::array<uint8_t, CHUNK_VOLUME> &data);
  /// FZ2.7-B1: bumped on any light-field mutation (Apply merge, relight seed).
  uint64_t GetLightFieldRevision() const { return LightFieldRevision; }
  uint64_t GetSkyLightRevision() const { return SkyLightRevision; }
  uint64_t GetBlockLightRevision() const { return BlockLightRevision; }
  uint64_t GetIncarnation() const { return Incarnation; }
  uint64_t GetContentRevision() const { return ContentRevision; }
  void BumpLightFieldRevision()
  {
    ++LightFieldRevision;
    InputRevision.NoteMutation();
  }

  /// Main-thread calculation provenance is owned by this resident generation.
  void ClearLightCalculationProofs() { LightProofs = {}; }

  static int LocalIndex(glm::ivec3 local);

  /// Memo of ChunkMeshLightHaloSignature per neighbor offset (index
  /// (dy+1)*9+(dz+1)*3+(dx+1)), valid for one (incarnation, light revision).
  /// Hashing the halo cells on every stamp validation / capture showed up as
  /// the top self cost of the streaming tick.
  bool TryGetCachedHaloSignature(int index, uint64_t &out) const
  {
    std::lock_guard<std::mutex> lock(HaloSigCache.Mutex);
    if (HaloSigCache.Valid[index] &&
        HaloSigCache.Incarnation[index] == Incarnation &&
        HaloSigCache.LightRev[index] == LightFieldRevision)
    {
      out = HaloSigCache.Sig[index];
      return true;
    }
    return false;
  }
  void StoreCachedHaloSignature(int index, uint64_t value) const
  {
    std::lock_guard<std::mutex> lock(HaloSigCache.Mutex);
    HaloSigCache.Valid[index] = true;
    HaloSigCache.Incarnation[index] = Incarnation;
    HaloSigCache.LightRev[index] = LightFieldRevision;
    HaloSigCache.Sig[index] = value;
  }

private:
  friend class UBlockWorld;
  friend class UChunkManager;
  /// Copies of voxel data do not acquire ownership of the source world's
  /// clock. Assignment preserves the destination binding and invalidates it.
  struct InputRevisionBinding
  {
    std::shared_ptr<uint64_t> clock;
    InputRevisionBinding() = default;
    InputRevisionBinding(const InputRevisionBinding &) {}
    InputRevisionBinding &operator=(const InputRevisionBinding &)
    {
      NoteMutation();
      return *this;
    }
    void NoteMutation() const
    {
      if (clock)
        ++*clock;
    }
  };
  InputRevisionBinding InputRevision;
  struct LightDomainProof
  {
    std::shared_ptr<const RelightComputeResult> input;
    std::shared_ptr<const void> owner;
    uint64_t incarnation{0};
    uint64_t field_revision{0};
  };
  std::array<LightDomainProof, 2> LightProofs;
  /// Copyable wrapper: a copy starts with an empty memo.
  struct HaloSignatureMemo
  {
    mutable std::mutex Mutex;
    bool Valid[27]{};
    uint64_t Incarnation[27]{};
    uint64_t LightRev[27]{};
    uint64_t Sig[27]{};
    HaloSignatureMemo() = default;
    HaloSignatureMemo(const HaloSignatureMemo &) {}
    HaloSignatureMemo &operator=(const HaloSignatureMemo &)
    {
      for (bool &v : Valid)
      {
        v = false;
      }
      return *this;
    }
  };
  mutable HaloSignatureMemo HaloSigCache;
  glm::ivec3 Coord;
  std::array<BlockId, CHUNK_VOLUME> Data{};
  std::array<uint8_t, CHUNK_VOLUME> FluidData{};
  std::array<uint8_t, CHUNK_VOLUME> LightData{};
  float SurfaceWetness{0.0f};
  double WeatherLastUpdateSec{0.0};
  uint64_t LightFieldRevision{0};
  uint64_t SkyLightRevision{0};
  uint64_t BlockLightRevision{0};
  inline static std::atomic<uint64_t> NextIncarnation{1};
  uint64_t Incarnation{NextIncarnation.fetch_add(1, std::memory_order_relaxed)};
  uint64_t ContentRevision{0};
  bool Dirty{true};
  /// Count of non-air blocks; IsAirOnly() when zero (avoids 4096-voxel scans).
  int NonAirCount{0};
};

} // namespace cutum

#endif
