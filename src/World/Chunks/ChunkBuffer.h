#pragma once

#include "World/Chunks/ChunkManager.h"
#include "World/Core/IUBlockWriter.h"
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
  BlockId GetBlock(glm::ivec3 worldPos) const override;
  uint8_t GetFluidPacked(glm::ivec3 worldPos) const;
  bool IsEmpty() const { return Blocks.empty(); }
  bool HasYBounds() const { return HasBounds; }
  int GetMinY() const { return MinY; }
  int GetMaxY() const { return MaxY; }
  void ApplyTo(UBlockWorld &world) const;
  void Clear();

private:
  std::unordered_map<glm::ivec3, BlockId, IVec3Hash> Blocks;
  std::unordered_map<glm::ivec3, uint8_t, IVec3Hash> FluidPacked;
  bool HasBounds{false};
  int MinY{0};
  int MaxY{-1};
};

} // namespace cutum
