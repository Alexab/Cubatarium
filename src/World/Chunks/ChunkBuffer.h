#pragma once

#include "World/Chunks/ChunkManager.h"
#include "World/Core/IUBlockWriter.h"
#include <array>
#include <unordered_map>

namespace cutum
{

class UBlockWorld;

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
  BlockId GetBlock(glm::ivec3 worldPos) const override;
  uint8_t GetFluidPacked(glm::ivec3 worldPos) const;
  uint8_t GetLightPacked(glm::ivec3 worldPos) const;
  bool HasChunkLightData() const { return HasChunkLight; }
  bool IsEmpty() const { return Blocks.empty() && !HasChunkLight; }
  bool HasYBounds() const { return HasBounds; }
  int GetMinY() const { return MinY; }
  int GetMaxY() const { return MaxY; }
  void ApplyTo(UBlockWorld &world) const;
  void Clear();

private:
  std::unordered_map<glm::ivec3, BlockId, IVec3Hash> Blocks;
  std::unordered_map<glm::ivec3, uint8_t, IVec3Hash> FluidPacked;
  std::unordered_map<glm::ivec3, uint8_t, IVec3Hash> LightPacked;
  bool HasChunkLight{false};
  glm::ivec3 ChunkLightCoord{0};
  std::array<uint8_t, CHUNK_VOLUME> ChunkLight{};
  bool HasBounds{false};
  int MinY{0};
  int MaxY{-1};
};

} // namespace cutum
