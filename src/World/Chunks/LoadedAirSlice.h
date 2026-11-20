#pragma once

#include "World/Chunks/Chunk.h"
#include <memory>

namespace cutum
{

/// Empty terrain data, not a light computation certificate. Codecs can mint
/// this after a full validated decode; storage can mint an exact absent slice
/// only inside an existing saved column with no pending save.
class LoadedAirSlice
{
public:
  glm::ivec3 GetCoord() const { return Coord; }

private:
  friend class UChunkBuffer;
  friend class UChunkStorageService;
  friend class UChunkManager;
  LoadedAirSlice() = default;
  glm::ivec3 Coord{0};
  std::shared_ptr<const std::array<uint8_t, CHUNK_VOLUME>> LegacyLight;
  bool HasWeather{false};
  float Wetness{0};
  double WeatherLastUpdateSec{0};
};

} // namespace cutum
