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
  SurfaceWetness = 0.0f;
  WeatherLastUpdateSec = 0.0;
}

UChunk &UChunk::operator=(const UChunk &other)
{
  if (this == &other)
    return *this;
  const bool managed = static_cast<bool>(InputRevision.clock);
  InputRevision.NoteGeometryMutation();
  if (!managed)
    Coord = other.Coord;
  Data = other.Data;
  FluidData = other.FluidData;
  Light = other.Light;
  SurfaceWetness = other.SurfaceWetness;
  WeatherLastUpdateSec = other.WeatherLastUpdateSec;
  ContentRevision = other.ContentRevision;
  Dirty = other.Dirty;
  NonAirCount = other.NonAirCount;
  Incarnation = managed
                    ? NextIncarnation.fetch_add(1, std::memory_order_relaxed)
                    : other.Incarnation;
  return *this;
}

void UChunk::ResetForReuse(glm::ivec3 chunkCoord)
{
  InputRevision.NoteGeometryMutation();
  *Light.field = SectionLightField{};
  Coord = chunkCoord;
  Data.fill(BLOCK_AIR);
  FluidData.fill(0);
  SurfaceWetness = 0.0f;
  WeatherLastUpdateSec = 0.0;
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
  InputRevision.NoteGeometryMutation();
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
    InputRevision.NoteGeometryMutation();
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
    InputRevision.NoteGeometryMutation();
  }
  Dirty = true;
}

bool UChunk::ReplaceLightData(const std::array<uint8_t, CHUNK_VOLUME> &data)
{
  if (!Light.field->Replace(data))
    return false;
  InputRevision.NoteMutation();
  return true;
}

uint8_t UChunk::GetLightPackedLocal(glm::ivec3 local) const
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return 0;
  }
  return Light.field->LightData[static_cast<size_t>(LocalIndex(local))];
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
  auto &packed = Light.field->LightData[static_cast<size_t>(LocalIndex(local))];
  const auto next = PackLight(sky_level, block_level);
  if (packed != next)
  {
    Light.field->SkyLightRevision += (packed & 0x0F) != (next & 0x0F);
    Light.field->BlockLightRevision += (packed & 0xF0) != (next & 0xF0);
    packed = next;
    ++Light.field->LightFieldRevision;
    InputRevision.NoteMutation();
  }
}

void UChunk::ClearLightLocal(glm::ivec3 local)
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return;
  }
  auto &packed = Light.field->LightData[static_cast<size_t>(LocalIndex(local))];
  if (packed != 0)
  {
    Light.field->SkyLightRevision += (packed & 0x0F) != 0;
    Light.field->BlockLightRevision += (packed & 0xF0) != 0;
    packed = 0;
    ++Light.field->LightFieldRevision;
    InputRevision.NoteMutation();
  }
}

} // namespace cutum
