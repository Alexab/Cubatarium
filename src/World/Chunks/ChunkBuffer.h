#pragma once

#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/LoadedAirSlice.h"
#include "World/Core/IUBlockWriter.h"
#include "Core/Memory/AllocationCountingResource.h"
#include <memory>
#include <array>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cutum
{

class UBlockWorld;
class UBlockRegistry;

class UBlockWorldWriter : public IUBlockWriter
{
public:
  explicit UBlockWorldWriter(UBlockWorld &world);

  void SetBlock(glm::ivec3 worldPos, BlockId id) override;
  BlockId GetBlock(glm::ivec3 worldPos) const override;

private:
  UBlockWorld &World;
};

class UChunkBuffer : public IUBlockWriter
{
public:
  UChunkBuffer() = default;
  UChunkBuffer(const UChunkBuffer &other);
  UChunkBuffer &operator=(const UChunkBuffer &other);
  UChunkBuffer(UChunkBuffer &&) noexcept = default;
  UChunkBuffer &operator=(UChunkBuffer &&) noexcept = default;
  UAllocationCountingResource::Snapshot GetAllocationSnapshot() const;
  size_t GetOwnedAllocationBytes() const;
  void SetBlock(glm::ivec3 worldPos, BlockId id) override;
  void SetFluidPacked(glm::ivec3 worldPos, uint8_t packed);
  void SetLightPacked(glm::ivec3 worldPos, uint8_t packed);
  void SetChunkLightData(glm::ivec3 chunkCoord,
                         const std::array<uint8_t, CHUNK_VOLUME> &light);
  void SetChunkWeatherState(glm::ivec3 chunkCoord, float wetness,
                            double last_update_sec);
  /// Mark a worker-generated buffer so object voxels crossing a chunk edge
  /// cannot race with the owning chunk's terrain when results are committed.
  void SetWorldGenOwnerChunk(
      glm::ivec3 chunkCoord, const UBlockRegistry &registry,
      const std::vector<std::pair<BlockId, int>> &treeBlockRanks = {});
  BlockId GetBlock(glm::ivec3 worldPos) const override;
  uint8_t GetFluidPacked(glm::ivec3 worldPos) const;
  uint8_t GetLightPacked(glm::ivec3 worldPos) const;
  bool HasChunkLightData() const { return Payload && Payload->State.HasChunkLight; }
  bool HasGeometryPayload() const
  {
    return Payload && (!Payload->Blocks.empty() || !Payload->FluidPacked.empty());
  }
  bool IsEmpty() const
  {
    return !Payload || (Payload->Blocks.empty() && Payload->LightPacked.empty() &&
        !Payload->State.HasChunkLight && !Payload->State.HasChunkWeather);
  }
  bool HasYBounds() const { return Payload && Payload->State.HasBounds; }
  int GetMinY() const { return Payload ? Payload->State.MinY : 0; }
  int GetMaxY() const { return Payload ? Payload->State.MaxY : -1; }
  void ApplyTo(UBlockWorld &world) const;
  /// Fast path for buffers known to contain one chunk (disk slice loads).
  void ApplyToChunk(UBlockWorld &world, glm::ivec3 chunkCoord) const;
  void Clear();
  /// Refuses unvalidated, subsequently edited, nonempty or foreign buffers.
  std::optional<LoadedAirSlice> GetDecodedAirSlice(glm::ivec3 coord) const;
  bool HasDecodedTerrainFrame(glm::ivec3 coord) const
  {
    return Payload && Payload->State.DecodedFrameValid && Payload->State.DecodedCoord == coord &&
        !Payload->State.HasWorldGenOwnerChunk;
  }

private:
  friend class UBinaryChunkSerializer;
  friend class UJsonChunkSerializer;
  struct FrameState
  {
    bool DecodedFrameValid{false};
    glm::ivec3 DecodedCoord{0};
    bool HasChunkLight{false};
    glm::ivec3 ChunkLightCoord{0};
    std::array<uint8_t, CHUNK_VOLUME> ChunkLight{};
    bool HasChunkWeather{false};
    glm::ivec3 ChunkWeatherCoord{0};
    float ChunkWetness{0.0f};
    double ChunkWeatherLastUpdateSec{0.0};
    bool HasBounds{false};
    int MinY{0}, MaxY{-1};
    glm::ivec3 WorldGenOwnerChunk{0};
    bool HasWorldGenOwnerChunk{false};
  };
  struct Storage
  {
    FrameState State;
    UAllocationCountingResource Memory;
    std::pmr::unordered_map<glm::ivec3,BlockId,IVec3Hash> Blocks{&Memory};
    std::pmr::unordered_map<glm::ivec3,uint8_t,IVec3Hash> FluidPacked{&Memory};
    std::pmr::unordered_map<glm::ivec3,uint8_t,IVec3Hash> LightPacked{&Memory};
    std::pmr::unordered_map<BlockId,int> WorldGenTreeBlockRanks{&Memory};
    Storage() = default;
    Storage(const Storage &other) : Storage()
    {
      State=other.State; Blocks=other.Blocks; FluidPacked=other.FluidPacked;
      LightPacked=other.LightPacked; WorldGenTreeBlockRanks=other.WorldGenTreeBlockRanks;
    }
  };
  std::unique_ptr<Storage> Payload;
  void EnsureStorage() { if(!Payload) Payload=std::make_unique<Storage>(); }
  void MarkDecodedFrame(glm::ivec3 coord)
  {
    EnsureStorage(); Payload->State.DecodedCoord=coord; Payload->State.DecodedFrameValid=true;
  }
};

} // namespace cutum
