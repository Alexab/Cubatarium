#pragma once

#include "World/Chunks/ChunkDimensions.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace cutum
{
struct RelightComputeResult;
class UBlockWorld;
class UChunk;
class UChunkManager;

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

/// Main-thread owned light resource. Geometry residency is a separate lifetime.
/// A copy is a detached byte snapshot with a fresh resource identity and no
/// calculation authority. Only world installation can grant that authority.
class SectionLightField
{
public:
  SectionLightField() = default;
  SectionLightField(const SectionLightField &other) { *this = other; }
  SectionLightField &operator=(const SectionLightField &other)
  {
    if (this == &other)
      return *this;
    LightData = other.LightData;
    LightFieldRevision = other.LightFieldRevision;
    SkyLightRevision = other.SkyLightRevision;
    BlockLightRevision = other.BlockLightRevision;
    Incarnation = NextIncarnation.fetch_add(1, std::memory_order_relaxed);
    LightProofs = {};
    HaloSigCache = other.HaloSigCache;
    return *this;
  }
  const std::array<uint8_t, CHUNK_VOLUME> &GetData() const { return LightData; }
  uint64_t GetIncarnation() const { return Incarnation; }
  uint64_t GetRevision() const { return LightFieldRevision; }
  uint64_t GetSkyRevision() const { return SkyLightRevision; }
  uint64_t GetBlockRevision() const { return BlockLightRevision; }

private:
  friend class UBlockWorld;
  friend class UChunk;
  friend class UChunkManager;
  bool Replace(const std::array<uint8_t, CHUNK_VOLUME> &data)
  {
    bool sky_changed = false, block_changed = false;
    for (size_t i = 0; i < data.size(); ++i)
    {
      sky_changed |= (data[i] & 0x0f) != (LightData[i] & 0x0f);
      block_changed |= (data[i] & 0xf0) != (LightData[i] & 0xf0);
    }
    if (!sky_changed && !block_changed)
      return false;
    LightData = data;
    ++LightFieldRevision;
    SkyLightRevision += sky_changed;
    BlockLightRevision += block_changed;
    return true;
  }
  struct LightDomainProof
  {
    std::shared_ptr<const RelightComputeResult> input;
    std::shared_ptr<const void> owner;
    uint64_t incarnation{0};
    uint64_t field_revision{0};
  };
  std::array<LightDomainProof, 2> LightProofs;
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
        v = false;
      return *this;
    }
  };
  mutable HaloSignatureMemo HaloSigCache;
  std::array<uint8_t, CHUNK_VOLUME> LightData{};
  uint64_t LightFieldRevision{0};
  uint64_t SkyLightRevision{0};
  uint64_t BlockLightRevision{0};
  inline static std::atomic<uint64_t> NextIncarnation{1};
  uint64_t Incarnation{NextIncarnation.fetch_add(1, std::memory_order_relaxed)};
};
} // namespace cutum
