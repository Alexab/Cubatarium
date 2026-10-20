#include "World/Chunks/Chunk.h"

#include "World/Lighting/LightUtil.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace cutum
{

UChunk::UChunk(glm::ivec3 chunkCoord) : Coord(chunkCoord)
{
  Data.fill(BLOCK_AIR);
  FluidData.fill(0);
  LightData.fill(0);
  SurfaceWetness = 0.0f;
  WeatherLastUpdateSec = 0.0;
}

void UChunk::ResetForReuse(glm::ivec3 chunkCoord)
{
  Coord = chunkCoord;
  Data.fill(BLOCK_AIR);
  FluidData.fill(0);
  LightData.fill(0);
  SurfaceWetness = 0.0f;
  WeatherLastUpdateSec = 0.0;
  LightFieldRevision = 0;
  ContentRevision = 0;
  Incarnation = NextIncarnation.fetch_add(1, std::memory_order_relaxed);
  Dirty = true;
  NonAirCount = 0;
}

void UChunk::SetWeatherState(float wetness, double last_update_sec)
{
  const float next_wetness =
      std::clamp(std::isfinite(wetness) ? wetness : 0.0f, 0.0f, 1.0f);
  const double next_time =
      std::isfinite(last_update_sec) ? std::max(0.0, last_update_sec) : 0.0;
  const bool wetness_changed = std::abs(next_wetness - SurfaceWetness) >= 0.01f;
  const bool save_interval_elapsed =
      std::floor(next_time / 30.0) != std::floor(WeatherLastUpdateSec / 30.0);
  SurfaceWetness = next_wetness;
  WeatherLastUpdateSec = next_time;
  if (wetness_changed || save_interval_elapsed)
  {
    Dirty = true;
  }
}

int UChunk::LocalIndex(glm::ivec3 local)
{
  return local.x + CHUNK_SIZE * local.y + CHUNK_SIZE * CHUNK_SIZE * local.z;
}

BlockId UChunk::GetBlockLocal(glm::ivec3 local) const
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return BLOCK_AIR;
  }
  return Data[static_cast<size_t>(LocalIndex(local))];
}

void UChunk::SetBlockLocal(glm::ivec3 local, BlockId Id)
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return;
  }
  const size_t idx = static_cast<size_t>(LocalIndex(local));
  const BlockId prev = Data[idx];
  if (prev == Id)
  {
    return;
  }
  Data[idx] = Id;
  ++ContentRevision;
  if (prev == BLOCK_AIR && Id != BLOCK_AIR)
  {
    ++NonAirCount;
  }
  else if (prev != BLOCK_AIR && Id == BLOCK_AIR)
  {
    --NonAirCount;
    if (NonAirCount < 0)
    {
      NonAirCount = 0;
    }
  }
  if (Id == BLOCK_AIR)
  {
    ClearFluidLocal(local);
  }
  Dirty = true;
}

FluidCellState UChunk::GetFluidLocal(glm::ivec3 local) const
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return {};
  }
  return UnpackFluidCellState(
      FluidData[static_cast<size_t>(LocalIndex(local))]);
}

void UChunk::SetFluidLocal(glm::ivec3 local, FluidCellState state)
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return;
  }
#ifndef NDEBUG
  assert(GetBlockLocal(local) != BLOCK_AIR);
#endif
  auto &packed = FluidData[static_cast<size_t>(LocalIndex(local))];
  const auto next = PackFluidCellState(state);
  if (packed != next)
  {
    packed = next;
    ++ContentRevision;
  }
  Dirty = true;
}

void UChunk::ClearFluidLocal(glm::ivec3 local)
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return;
  }
  auto &packed = FluidData[static_cast<size_t>(LocalIndex(local))];
  if (packed != 0)
  {
    packed = 0;
    ++ContentRevision;
  }
  Dirty = true;
}

uint8_t UChunk::GetLightPackedLocal(glm::ivec3 local) const
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return 0;
  }
  return LightData[static_cast<size_t>(LocalIndex(local))];
}

int UChunk::GetSkyLightLocal(glm::ivec3 local) const
{
  return UnpackSky(GetLightPackedLocal(local));
}

int UChunk::GetBlockLightLocal(glm::ivec3 local) const
{
  return UnpackBlock(GetLightPackedLocal(local));
}

void UChunk::SetLightLocal(glm::ivec3 local, int sky_level, int block_level)
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return;
  }
  auto &packed = LightData[static_cast<size_t>(LocalIndex(local))];
  const auto next = PackLight(sky_level, block_level);
  if (packed != next)
  {
    packed = next;
    ++LightFieldRevision;
  }
}

void UChunk::ClearLightLocal(glm::ivec3 local)
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return;
  }
  auto &packed = LightData[static_cast<size_t>(LocalIndex(local))];
  if (packed != 0)
  {
    packed = 0;
    ++LightFieldRevision;
  }
}

} // namespace cutum
