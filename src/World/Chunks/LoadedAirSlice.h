#pragma once

#include "World/Chunks/Chunk.h"
#include <memory>

namespace cutum
{

/// Decoded empty geometry, not a light computation certificate. Only codecs
/// through UChunkBuffer can mint this value after validating the whole frame.
class LoadedAirSlice
{
public:
  glm::ivec3 GetCoord() const { return Coord; }

private:
  friend class UChunkBuffer;
  friend class UChunkManager;
  LoadedAirSlice() = default;
  glm::ivec3 Coord{0};
  std::shared_ptr<const std::array<uint8_t, CHUNK_VOLUME>> LegacyLight;
  bool HasWeather{false};
  float Wetness{0};
  double WeatherLastUpdateSec{0};
};

} // namespace cutum
