#ifndef LIGHTUTIL_H
#define LIGHTUTIL_H

#include "Blocks/BlockRegistry.h"
#include "World/Math/BlockTypes.h"
#include <algorithm>
#include <cstdint>

namespace cutum
{

constexpr int kMaxLightLevel = 15;

inline uint8_t PackLight(int sky_level, int block_level)
{
  const int sky = std::clamp(sky_level, 0, kMaxLightLevel);
  const int block = std::clamp(block_level, 0, kMaxLightLevel);
  return static_cast<uint8_t>((sky & 0x0F) | ((block & 0x0F) << 4));
}

inline int UnpackSky(uint8_t packed)
{
  return static_cast<int>(packed & 0x0F);
}

inline int UnpackBlock(uint8_t packed)
{
  return static_cast<int>((packed >> 4) & 0x0F);
}

bool IsLightTransparent(const UBlockRegistry &registry, BlockId id);

} // namespace cutum

#endif // LIGHTUTIL_H
