#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Chunks/ChunkResidencyTrace.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <vector>

namespace cutum
{

glm::ivec3 UChunkManager::WorldToChunk(glm::ivec3 worldPos)
{
  return glm::ivec3(FloorDiv(worldPos.x, CHUNK_SIZE),
                    FloorDiv(worldPos.y, CHUNK_SIZE),
                    FloorDiv(worldPos.z, CHUNK_SIZE));
}

glm::ivec3 UChunkManager::WorldToLocal(glm::ivec3 worldPos)
{
  return glm::ivec3(PositiveMod(worldPos.x, CHUNK_SIZE),
                    PositiveMod(worldPos.y, CHUNK_SIZE),
                    PositiveMod(worldPos.z, CHUNK_SIZE));
}

BlockId UChunkManager::GetBlock(glm::ivec3 worldPos) const
{
  // Legacy SoT: missing chunk still reports AIR for worldgen/light probes.
  // Prefer QueryBlock for interaction/raycast (Unloaded ≠ AIR).
  return QueryBlock(worldPos).id;
}

BlockQueryResult UChunkManager::QueryBlock(glm::ivec3 worldPos) const
{
  const glm::ivec3 chunkCoord = WorldToChunk(worldPos);
  auto it = Chunks.find(chunkCoord);
  if (it == Chunks.end())
  {
    return HasLoadedAirSlice(chunkCoord) ? MakeAirQuery() : MakeUnloadedQuery();
  }
  return MakeSolidQuery(it->second->GetBlockLocal(WorldToLocal(worldPos)));
}

FluidCellState UChunkManager::GetFluidState(glm::ivec3 worldPos) const
{
  const glm::ivec3 chunkCoord = WorldToChunk(worldPos);
  auto it = Chunks.find(chunkCoord);
  if (it == Chunks.end())
  {
    return {};
  }
  return it->second->GetFluidLocal(WorldToLocal(worldPos));
}

void UChunkManager::SetBlock(glm::ivec3 worldPos, BlockId Id)
{
  const glm::ivec3 chunkCoord = WorldToChunk(worldPos);
  if (TraceChunkResidencyEnabled(chunkCoord) && !HasChunk(chunkCoord))
    TraceChunkResidency("set_block_missing", chunkCoord, worldPos, Id, this);
  UChunk &chunk = GetOrCreateChunk(chunkCoord);
  const bool was_air_only = chunk.IsAirOnly();
  chunk.SetBlockLocal(WorldToLocal(worldPos), Id);
  if (was_air_only && !chunk.IsAirOnly() && OnChunkBecameNonAir)
  {
    OnChunkBecameNonAir(chunkCoord);
  }
}

void UChunkManager::SetFluidState(glm::ivec3 worldPos, FluidCellState state)
{
  const auto coord = WorldToChunk(worldPos);
  if (TraceChunkResidencyEnabled(coord) && !HasChunk(coord))
    TraceChunkResidency("set_fluid_missing", coord, worldPos,
                        PackFluidCellState(state), this);
  UChunk &chunk = GetOrCreateChunk(coord);
  chunk.SetFluidLocal(WorldToLocal(worldPos), state);
}

void UChunkManager::ClearFluidState(glm::ivec3 worldPos)
{
  const glm::ivec3 chunkCoord = WorldToChunk(worldPos);
  auto it = Chunks.find(chunkCoord);
  if (it == Chunks.end())
  {
    return;
  }
  it->second->ClearFluidLocal(WorldToLocal(worldPos));
}

void UChunkManager::Clear()
{
  for (const auto &entry : LoadedAirSlices)
    if (const auto field = GetSectionLightField(entry.first))
      TraceChunkResidency("air_clear", entry.first, entry.first,
                          field->GetIncarnation(), this);
  NoteInputMutation();
  Chunks.clear();
  LightFields.clear();
  LoadedAirSlices.clear();
  AbsentLightFieldScan.clear();
  ResidentColumnChunkCounts.clear();
  FreeList.clear();
}

void UChunkManager::ForEachBlock(
    const std::function<void(glm::ivec3, BlockId)> &fn) const
{
  for (const auto &entry : Chunks)
  {
    const glm::ivec3 chunkCoord = entry.first;
    const UChunk &chunk = *entry.second;
    for (int z = 0; z < CHUNK_SIZE; ++z)
    {
      for (int y = 0; y < CHUNK_SIZE; ++y)
      {
        for (int x = 0; x < CHUNK_SIZE; ++x)
        {
          const glm::ivec3 local(x, y, z);
          const BlockId Id = chunk.GetBlockLocal(local);
          if (Id == BLOCK_AIR)
          {
            continue;
          }
          const glm::ivec3 worldPos(chunkCoord.x * CHUNK_SIZE + x,
                                    chunkCoord.y * CHUNK_SIZE + y,
                                    chunkCoord.z * CHUNK_SIZE + z);
          fn(worldPos, Id);
        }
      }
    }
  }
}

UChunk *UChunkManager::GetChunk(glm::ivec3 chunkCoord)
{
  auto it = Chunks.find(chunkCoord);
  if (it == Chunks.end())
  {
    return nullptr;
  }
  return it->second.get();
}

const UChunk *UChunkManager::GetChunk(glm::ivec3 chunkCoord) const
{
  auto it = Chunks.find(chunkCoord);
  if (it == Chunks.end())
  {
    return nullptr;
  }
  return it->second.get();
}

bool UChunkManager::HasChunk(glm::ivec3 chunkCoord) const
{
  return Chunks.find(chunkCoord) != Chunks.end();
}

bool UChunkManager::HasLoadedAirSlice(glm::ivec3 coord) const
{
  return LoadedAirSlices.find(coord) != LoadedAirSlices.end();
}

bool UChunkManager::InstallLoadedAirSlice(const LoadedAirSlice &slice,
                                          const ChunkGenerationToken &token,
                                          uint64_t current_sequence,
                                          bool cancelled)
{
  const auto coord = slice.Coord;
  if (cancelled || !token.IsValidFor({coord.x, 0, coord.z}, current_sequence) ||
      HasChunk(coord) || HasLoadedAirSlice(coord))
    return false;
  // Never inherit a computed proof from an earlier field-only owner. Loading
  // legacy light bytes is not a new physical calculation.
  auto field = std::make_shared<SectionLightField>();
  if (slice.LegacyLight)
    field->Replace(*slice.LegacyLight);
  LightFields[coord] = field;
  auto metadata = slice;
  metadata.LegacyLight.reset(); // canonical bytes now belong to LightFields
  LoadedAirSlices.emplace(coord, std::move(metadata));
  AbsentLightFieldScan.emplace_back(coord, field->GetIncarnation());
  NoteInputMutation();
  TraceChunkResidency("air_install", coord, coord, field->GetIncarnation(),
                      this);
  return true;
}

std::unique_ptr<UChunk>
UChunkManager::MakeTerrainSaveSnapshot(glm::ivec3 coord) const
{
  if (const auto *chunk = GetChunk(coord))
    return std::make_unique<UChunk>(*chunk);
  const auto air = LoadedAirSlices.find(coord);
  if (air == LoadedAirSlices.end())
    return nullptr;
  auto result = std::make_unique<UChunk>(coord);
  if (const auto field = GetSectionLightField(coord))
    result->ReplaceLightData(field->GetData());
  if (air->second.HasWeather)
    result->SetWeatherState(air->second.Wetness,
                            air->second.WeatherLastUpdateSec);
  return result;
}

std::shared_ptr<const SectionLightField>
UChunkManager::GetSectionLightField(glm::ivec3 coord) const
{
  const auto found = LightFields.find(coord);
  return found == LightFields.end() ? nullptr : found->second;
}

std::shared_ptr<const SectionLightField>
UChunkManager::EnsureSectionLightField(glm::ivec3 coord)
{
  const auto found = LightFields.find(coord);
  if (found != LightFields.end()) return found->second;
  auto field = std::make_shared<SectionLightField>();
  LightFields.emplace(coord, field);
  if (!HasChunk(coord))
    AbsentLightFieldScan.emplace_back(coord, field->GetIncarnation());
  NoteLightInputMutation();
  return field;
}

bool UChunkManager::ReplaceSectionLightData(
    glm::ivec3 coord, const std::array<uint8_t, CHUNK_VOLUME> &data)
{
  EnsureSectionLightField(coord);
  if (!LightFields.at(coord)->Replace(data))
    return false;
  NoteLightInputMutation();
  return true;
}

bool UChunkManager::ReplaceComputedSectionLightVolume(
    glm::ivec3 coord, const SectionLightVolume &volume, bool sky, bool block)
{
  if (!sky && !block)
    return false;
  EnsureSectionLightField(coord);
  if (!LightFields.at(coord)->ReplaceComputedVolume(volume, sky, block))
    return false;
  NoteLightInputMutation();
  return true;
}

bool UChunkManager::ReleaseAbsentSectionLightField(glm::ivec3 coord)
{
  if (HasChunk(coord) || HasLoadedAirSlice(coord) || !LightFields.erase(coord))
    return false;
  NoteLightInputMutation();
  return true;
}

size_t UChunkManager::RetireAbsentSectionLightFields(
    size_t visit_budget, const std::function<bool(glm::ivec3)> &keep)
{
  const size_t visits = std::min(visit_budget, AbsentLightFieldScan.size());
  size_t retired = 0;
  for (size_t i = 0; i < visits; ++i)
  {
    const auto entry = AbsentLightFieldScan.front();
    AbsentLightFieldScan.pop_front();
    const auto field = LightFields.find(entry.first);
    if (field == LightFields.end() ||
        field->second->GetIncarnation() != entry.second ||
        HasChunk(entry.first))
      continue;
    if (HasLoadedAirSlice(entry.first) || keep(entry.first))
      AbsentLightFieldScan.push_back(entry);
    else
    {
      LightFields.erase(field);
      ++retired;
    }
  }
  if (retired)
    NoteLightInputMutation();
  return retired;
}

void UChunkManager::ForEachChunk(
    const std::function<void(const UChunk &)> &fn) const
{
  for (const auto &entry : Chunks)
  {
    fn(*entry.second);
  }
}

void UChunkManager::ForEachColumn(
    const std::function<void(const glm::ivec3 &)> &fn) const
{
  for (const auto &entry : ResidentColumnChunkCounts)
  {
    fn(entry.first);
  }
}

void UChunkManager::RemoveChunk(glm::ivec3 chunkCoord)
{
  if (LoadedAirSlices.erase(chunkCoord))
  {
    if (const auto field = GetSectionLightField(chunkCoord))
      TraceChunkResidency("air_remove", chunkCoord, chunkCoord,
                          field->GetIncarnation(), this);
    LightFields.erase(chunkCoord);
    NoteInputMutation();
  }
  auto it = Chunks.find(chunkCoord);
  if (it == Chunks.end())
  {
    return;
  }
  TraceChunkResidency("remove", chunkCoord, chunkCoord,
                      it->second->GetIncarnation(), this);
  NoteInputMutation();
  std::unique_ptr<UChunk> chunk = std::move(it->second);
  chunk->ClearLightCalculationProofs();
  Chunks.erase(it);
  LightFields.erase(chunkCoord);
  const glm::ivec3 groundCoord(chunkCoord.x, 0, chunkCoord.z);
  auto columnIt = ResidentColumnChunkCounts.find(groundCoord);
  if (columnIt != ResidentColumnChunkCounts.end())
  {
    if (columnIt->second <= 1)
    {
      ResidentColumnChunkCounts.erase(columnIt);
    }
    else
    {
      --columnIt->second;
    }
  }
  if (chunk && FreeList.size() < MaxFreeListChunks)
  {
    FreeList.push_back(std::move(chunk));
  }
}

void UChunkManager::EnsureChunk(glm::ivec3 chunkCoord)
{
  if (TraceChunkResidencyEnabled(chunkCoord) && !HasChunk(chunkCoord))
    TraceChunkResidency("ensure_missing", chunkCoord, chunkCoord, 0, this);
  (void)GetOrCreateChunk(chunkCoord);
}

UChunk &UChunkManager::GetOrCreateChunk(glm::ivec3 chunkCoord)
{
  auto it = Chunks.find(chunkCoord);
  if (it != Chunks.end())
  {
    return *it->second;
  }
  std::unique_ptr<UChunk> chunk;
  if (!FreeList.empty())
  {
    chunk = std::move(FreeList.back());
    FreeList.pop_back();
    chunk->Light.field = std::make_shared<SectionLightField>();
    chunk->ResetForReuse(chunkCoord);
  }
  else
  {
    chunk = std::make_unique<UChunk>(chunkCoord);
  }
  TraceChunkResidency("create", chunkCoord, chunkCoord, chunk->GetIncarnation(),
                      this);
  NoteInputMutation();
  auto [field_it, field_inserted] =
      LightFields.emplace(chunkCoord, chunk->Light.field);
  if (!field_inserted)
    chunk->Light.field = field_it->second;
  if (const auto air = LoadedAirSlices.find(chunkCoord);
      air != LoadedAirSlices.end())
  {
    TraceChunkResidency("air_materialize", chunkCoord, chunkCoord,
                        field_it->second->GetIncarnation(), this);
    if (air->second.HasWeather)
      chunk->SetWeatherState(air->second.Wetness,
                             air->second.WeatherLastUpdateSec);
    LoadedAirSlices.erase(air);
  }
  chunk->InputRevision.clock = InputRevision;
  chunk->InputRevision.geometry_clock = GeometryInputRevision;
  const auto inserted = Chunks.emplace(chunkCoord, std::move(chunk));
  const glm::ivec3 groundCoord(chunkCoord.x, 0, chunkCoord.z);
  if (inserted.second)
  {
    ++ResidentColumnChunkCounts[groundCoord];
  }
  return *inserted.first->second;
}

} // namespace cutum
