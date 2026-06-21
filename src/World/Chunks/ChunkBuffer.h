#pragma once

#include "World/Core/IBlockWriter.h"
#include <unordered_map>

namespace cutum
{

class UBlockWorld;

class BlockWorldWriter : public IBlockWriter
{
public:
  explicit BlockWorldWriter(UBlockWorld &world);

  void SetBlock(glm::ivec3 worldPos, BlockId id) override;
  BlockId GetBlock(glm::ivec3 worldPos) const override;

private:
  UBlockWorld &World;
};

class ChunkBuffer : public IBlockWriter
{
public:
  void SetBlock(glm::ivec3 worldPos, BlockId id) override;
  BlockId GetBlock(glm::ivec3 worldPos) const override;
  bool IsEmpty() const { return Blocks.empty(); }
  void ApplyTo(UBlockWorld &world) const;
  void Clear();

private:
  std::unordered_map<glm::ivec3, BlockId, IVec3Hash> Blocks;
};

} // namespace cutum
