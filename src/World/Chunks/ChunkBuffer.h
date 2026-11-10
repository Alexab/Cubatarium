#pragma once

#include "World/Chunks/ChunkManager.h"
#include "World/Core/IUBlockWriter.h"
#include <array>
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
  bool HasChunkLightData() const { return HasChunkLight; }
  bool IsEmpty() const
  {
    return Blocks.empty() && !HasChunkLight && !HasChunkWeather;
  }
  bool HasYBounds() const { return HasBounds; }
  int GetMinY() const { return MinY; }
  int GetMaxY() const { return MaxY; }
  void ApplyTo(UBlockWorld &world) const;
  /// Fast path for buffers known to contain one chunk (disk slice loads).
  void ApplyToChunk(UBlockWorld &world, glm::ivec3 chunkCoord) const;
  void Clear();

private:
  std::unordered_map<glm::ivec3, BlockId, IVec3Hash> Blocks;
  std::unordered_map<glm::ivec3, uint8_t, IVec3Hash> FluidPacked;
  std::unordered_map<glm::ivec3, uint8_t, IVec3Hash> LightPacked;
  bool HasChunkLight{false};
  glm::ivec3 ChunkLightCoord{0};
  std::array<uint8_t, CHUNK_VOLUME> ChunkLight{};
  bool HasChunkWeather{false};
  glm::ivec3 ChunkWeatherCoord{0};
  float ChunkWetness{0.0f};
  double ChunkWeatherLastUpdateSec{0.0};
  bool HasBounds{false};
  int MinY{0};
  int MaxY{-1};
  glm::ivec3 WorldGenOwnerChunk{0};
  bool HasWorldGenOwnerChunk{false};
  std::unordered_map<BlockId, int> WorldGenTreeBlockRanks;
};

} // namespace cutum
