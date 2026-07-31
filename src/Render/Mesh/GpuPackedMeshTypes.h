#pragma once

#include "World/Math/BlockTypes.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

struct GpuBlockDrawRange
{
  BlockId blockId{BLOCK_AIR};
  uint32_t quadOffset{0};
  uint32_t quadCount{0};
  bool Transparent{false};
  bool AlphaCutout{false};
};

struct GpuPackedChunkRef
{
  glm::ivec3 chunkCoord{0};
  int slotIndex{-1};
  std::vector<GpuBlockDrawRange> blockRanges;
};

struct GpuMeshProcessResult
{
  bool success{false};
  int slotIndex{-1};
  uint32_t quadCount{0};
  bool transparent{false};
  bool hasFullyDarkFace{false};
  std::vector<GpuBlockDrawRange> blockRanges;
};

} // namespace cutum
