#include "World/Chunks/ChunkManager.h"
#include "World/Math/GridMath.h"
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
    return MakeUnloadedQuery();
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
  UChunk &chunk = GetOrCreateChunk(WorldToChunk(worldPos));
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
  NoteInputMutation();
  Chunks.clear();
  LightFields.clear();
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

bool UChunkManager::ReleaseAbsentSectionLightField(glm::ivec3 coord)
{
  if (HasChunk(coord) || !LightFields.erase(coord))
    return false;
  NoteLightInputMutation();
  return true;
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
  auto it = Chunks.find(chunkCoord);
  if (it == Chunks.end())
  {
    return;
  }
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
  NoteInputMutation();
  auto [field_it, field_inserted] =
      LightFields.emplace(chunkCoord, chunk->Light.field);
  if (!field_inserted)
    chunk->Light.field = field_it->second;
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
