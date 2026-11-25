#pragma once

#include "World/Chunks/ChunkDimensions.h"
#include "World/Lighting/SectionLightVolume.h"
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
    PaddedLight = other.PaddedLight;
    PaddedDomains = other.PaddedDomains;
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

  std::shared_ptr<const SectionLightVolume> GetPaddedData() const
  {
    return PaddedLight;
  }
  uint8_t GetPaddedDomains() const { return PaddedDomains; }

  // Quiescent owner-thread diagnostic. Include stale proofs too: validity
  // checks do not change the lifetime of their immutable input owners.
  template<class Inspector> void InspectCatalogRoots(Inspector &out) const
  {for(const auto &proof:LightProofs)out.RelightResult(proof.input);}

private:
  friend class UBlockWorld;
  friend class UChunk;
  friend class UChunkManager;
  bool ReplaceComputedVolume(const SectionLightVolume &volume, bool sky,
                             bool block)
  {
    const uint8_t mask = (sky ? 0x0f : 0) | (block ? 0xf0 : 0);
    if (!mask)
      return false;
    auto merged = std::make_shared<SectionLightVolume>();
    if (PaddedLight)
      *merged = *PaddedLight;
    bool sky_changed = sky && !(PaddedDomains & 1);
    bool block_changed = block && !(PaddedDomains & 2);
    for (size_t i = 0; i < volume.cells.size(); ++i)
    {
      const auto previous = merged->cells[i];
      const uint8_t next = (previous & ~mask) | (volume.cells[i] & mask);
      sky_changed |= ((previous ^ next) & 0x0f) != 0;
      block_changed |= ((previous ^ next) & 0xf0) != 0;
      merged->cells[i] = next;
    }
    for (int z = 0; z < CHUNK_SIZE; ++z)
      for (int y = 0; y < CHUNK_SIZE; ++y)
        for (int x = 0; x < CHUNK_SIZE; ++x)
        {
          const auto i = x + CHUNK_SIZE * (y + CHUNK_SIZE * z);
          const uint8_t next =
              (LightData[i] & ~mask) |
              (volume.cells[SectionLightVolume::Index(x, y, z)] & mask);
          sky_changed |= ((LightData[i] ^ next) & 0x0f) != 0;
          block_changed |= ((LightData[i] ^ next) & 0xf0) != 0;
          LightData[i] = next;
        }
    if (!sky_changed && !block_changed)
      return false;
    PaddedLight = std::move(merged);
    PaddedDomains |= (sky ? 1 : 0) | (block ? 2 : 0);
    ++LightFieldRevision;
    SkyLightRevision += sky_changed;
    BlockLightRevision += block_changed;
    return true;
  }
  void InvalidatePaddedDomains(bool sky, bool block)
  {
    PaddedDomains &= ~((sky ? 1 : 0) | (block ? 2 : 0));
  }
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
    InvalidatePaddedDomains(sky_changed, block_changed);
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
    bool padded_installed{false};
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
  std::shared_ptr<const SectionLightVolume> PaddedLight;
  uint8_t PaddedDomains{0};
  uint64_t LightFieldRevision{0};
  uint64_t SkyLightRevision{0};
  uint64_t BlockLightRevision{0};
  inline static std::atomic<uint64_t> NextIncarnation{1};
  uint64_t Incarnation{NextIncarnation.fetch_add(1, std::memory_order_relaxed)};
};
} // namespace cutum
