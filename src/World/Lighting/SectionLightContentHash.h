#pragma once
#include "World/Lighting/SectionLightVolume.h"
#include "World/Math/BlockTypes.h"

namespace cutum
{
inline uint64_t
HashSectionGeometryBytes(const std::array<BlockId, CHUNK_VOLUME> *data)
{
  uint64_t hash = 14695981039346656037ull;
  for (size_t n = 0; n < CHUNK_VOLUME; ++n)
    for (int shift : {0, 8})
    {
      const uint16_t id = data ? (*data)[n] : BLOCK_AIR;
      hash ^= static_cast<uint8_t>(id >> shift);
      hash *= 1099511628211ull;
    }
  return hash;
}
inline uint64_t
SectionGeometryContentHash(const std::array<BlockId, CHUNK_VOLUME> *data)
{
  static const uint64_t air = HashSectionGeometryBytes(nullptr);
  return data ? HashSectionGeometryBytes(data) : air;
}
inline uint64_t SectionLightDomainHash(const SectionLightVolume &volume,
                                       int domain)
{
  const uint8_t mask = domain == 0 ? 0x0f : 0xf0;
  uint64_t hash = 14695981039346656037ull;
  for (const uint8_t cell : volume.cells)
  {
    hash ^= cell & mask;
    hash *= 1099511628211ull;
  }
  return hash;
}
} // namespace cutum
