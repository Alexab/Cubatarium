#include "World/Chunks/ChunkBuffer.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace cutum
{

namespace
{

int TreeFeatureBlockRank(BlockId id,
                         const std::unordered_map<BlockId, int> &treeBlockRanks)
{
  if (id == BLOCK_AIR)
  {
    return 0;
  }
  const auto it = treeBlockRanks.find(id);
  return it == treeBlockRanks.end() ? 0 : it->second;
}

bool ShouldApplyCrossChunkBlock(
    const UBlockWorld &world,
    const std::unordered_map<BlockId, int> &treeBlockRanks, glm::ivec3 pos,
    BlockId incoming)
{
  const BlockId current = world.GetBlock(pos);
  if (current == BLOCK_AIR)
  {
    return true;
  }
  if (current == incoming)
  {
    return false;
  }

  const int currentRank = TreeFeatureBlockRank(current, treeBlockRanks);
  const int incomingRank = TreeFeatureBlockRank(incoming, treeBlockRanks);
  if (currentRank == 0 || incomingRank == 0)
  {
    // The target chunk owns its terrain and any features already committed
    // there. Cross-boundary object voxels only fill empty cells.
    return false;
  }

  if (incomingRank != currentRank)
  {
    return incomingRank > currentRank;
  }
  return incoming < current;
}

} // namespace

UBlockWorldWriter::UBlockWorldWriter(UBlockWorld &world) : World(world) {}

void UBlockWorldWriter::SetBlock(glm::ivec3 worldPos, BlockId id)
{
  World.SetBlock(worldPos, id);
}

BlockId UBlockWorldWriter::GetBlock(glm::ivec3 worldPos) const
{
  return World.GetBlock(worldPos);
}

void UChunkBuffer::SetBlock(glm::ivec3 worldPos, BlockId id)
{
  if (id == BLOCK_AIR)
  {
    Blocks.erase(worldPos);
    FluidPacked.erase(worldPos);
    LightPacked.erase(worldPos);
    return;
  }
  Blocks[worldPos] = id;
  if (!HasBounds)
  {
    HasBounds = true;
    MinY = worldPos.y;
    MaxY = worldPos.y;
    return;
  }
  MinY = std::min(MinY, worldPos.y);
  MaxY = std::max(MaxY, worldPos.y);
}

void UChunkBuffer::SetFluidPacked(glm::ivec3 worldPos, uint8_t packed)
{
  if (packed == 0)
  {
    FluidPacked.erase(worldPos);
    return;
  }
  FluidPacked[worldPos] = packed;
}

void UChunkBuffer::SetLightPacked(glm::ivec3 worldPos, uint8_t packed)
{
  if (packed == 0)
  {
    LightPacked.erase(worldPos);
    return;
  }
  LightPacked[worldPos] = packed;
}

void UChunkBuffer::SetChunkLightData(
    glm::ivec3 chunkCoord, const std::array<uint8_t, CHUNK_VOLUME> &light)
{
  HasChunkLight = true;
  ChunkLightCoord = chunkCoord;
  ChunkLight = light;
}

void UChunkBuffer::SetChunkWeatherState(glm::ivec3 chunkCoord, float wetness,
                                        double last_update_sec)
{
  HasChunkWeather = true;
  ChunkWeatherCoord = chunkCoord;
  ChunkWetness =
      std::clamp(std::isfinite(wetness) ? wetness : 0.0f, 0.0f, 1.0f);
  ChunkWeatherLastUpdateSec =
      std::isfinite(last_update_sec) ? std::max(0.0, last_update_sec) : 0.0;
}

void UChunkBuffer::SetWorldGenOwnerChunk(
    glm::ivec3 chunkCoord, const UBlockRegistry &registry,
    const std::vector<std::pair<BlockId, int>> &treeBlockRanks)
{
  WorldGenOwnerChunk = chunkCoord;
  HasWorldGenOwnerChunk = true;
  WorldGenTreeBlockRanks.clear();
  const std::array<std::pair<const char *, int>, 4> builtins = {
      {{"tree_log", 3},
       {"tree_bark", 3},
       {"tree_branch", 2},
       {"tree_leaves", 1}}};
  for (const auto &[name, rank] : builtins)
  {
    const BlockId id = registry.GetIdByTypeName(name);
    if (id != BLOCK_AIR)
    {
      WorldGenTreeBlockRanks[id] = rank;
    }
  }
  for (const auto &[id, rank] : treeBlockRanks)
  {
    if (id != BLOCK_AIR && rank > 0)
    {
      WorldGenTreeBlockRanks[id] = std::max(WorldGenTreeBlockRanks[id], rank);
    }
  }
}

BlockId UChunkBuffer::GetBlock(glm::ivec3 worldPos) const
{
  const auto it = Blocks.find(worldPos);
  if (it == Blocks.end())
  {
    return BLOCK_AIR;
  }
  return it->second;
}

uint8_t UChunkBuffer::GetFluidPacked(glm::ivec3 worldPos) const
{
  const auto it = FluidPacked.find(worldPos);
  if (it == FluidPacked.end())
  {
    return 0;
  }
  return it->second;
}

uint8_t UChunkBuffer::GetLightPacked(glm::ivec3 worldPos) const
{
  if (HasChunkLight)
  {
    const glm::ivec3 local = worldPos - ChunkLightCoord * CHUNK_SIZE;
    if (local.x >= 0 && local.x < CHUNK_SIZE && local.y >= 0 &&
        local.y < CHUNK_SIZE && local.z >= 0 && local.z < CHUNK_SIZE)
    {
      const int index = local.x + CHUNK_SIZE * (local.y + CHUNK_SIZE * local.z);
      return ChunkLight[static_cast<size_t>(index)];
    }
  }
  const auto it = LightPacked.find(worldPos);
  if (it == LightPacked.end())
  {
    return 0;
  }
  return it->second;
}

void UChunkBuffer::ApplyTo(UBlockWorld &world) const
{
  for (const auto &entry : Blocks)
  {
    const bool ownerChunk =
        !HasWorldGenOwnerChunk ||
        UChunkManager::WorldToChunk(entry.first) == WorldGenOwnerChunk;
    const int incomingTreeRank =
        TreeFeatureBlockRank(entry.second, WorldGenTreeBlockRanks);
    const int currentTreeRank = TreeFeatureBlockRank(
        world.GetBlock(entry.first), WorldGenTreeBlockRanks);
    if ((HasWorldGenOwnerChunk && !ownerChunk &&
         !ShouldApplyCrossChunkBlock(world, WorldGenTreeBlockRanks, entry.first,
                                     entry.second)) ||
        (ownerChunk && incomingTreeRank > 0 && currentTreeRank > 0 &&
         !ShouldApplyCrossChunkBlock(world, WorldGenTreeBlockRanks, entry.first,
                                     entry.second)))
    {
      continue;
    }
    world.SetBlock(entry.first, entry.second);
    const auto fluid_it = FluidPacked.find(entry.first);
    if (fluid_it != FluidPacked.end() && fluid_it->second != 0)
    {
      world.SetFluidState(entry.first, UnpackFluidCellState(fluid_it->second));
    }
  }
  if (HasChunkLight)
  {
    world.GetChunkManager().EnsureChunk(ChunkLightCoord);
    if (UChunk *chunk = world.GetChunkManager().GetChunk(ChunkLightCoord))
    {
      chunk->GetLightDataMutable() = ChunkLight;
    }
  }
  if (HasChunkWeather)
  {
    world.GetChunkManager().EnsureChunk(ChunkWeatherCoord);
    if (UChunk *chunk = world.GetChunkManager().GetChunk(ChunkWeatherCoord))
    {
      chunk->SetWeatherState(ChunkWetness, ChunkWeatherLastUpdateSec);
    }
  }
  for (const auto &entry : LightPacked)
  {
    const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(entry.first);
    world.GetChunkManager().EnsureChunk(chunk_coord);
    UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
    if (!chunk)
    {
      continue;
    }
    const glm::ivec3 local = UChunkManager::WorldToLocal(entry.first);
    chunk->GetLightDataMutable()[static_cast<size_t>(
        UChunk::LocalIndex(local))] = entry.second;
  }
}

void UChunkBuffer::ApplyToChunk(UBlockWorld &world, glm::ivec3 chunkCoord) const
{
  if (IsEmpty())
  {
    return;
  }

  UChunkManager &chunks = world.GetChunkManager();
  chunks.EnsureChunk(chunkCoord);
  UChunk *chunk = chunks.GetChunk(chunkCoord);
  if (!chunk)
  {
    return;
  }

  for (const auto &entry : Blocks)
  {
    assert(UChunkManager::WorldToChunk(entry.first) == chunkCoord);
    world.SetBlockInChunk(*chunk, entry.first, entry.second);
    const auto fluid_it = FluidPacked.find(entry.first);
    if (fluid_it != FluidPacked.end() && fluid_it->second != 0)
    {
      world.SetFluidStateInChunk(*chunk, entry.first,
                                 UnpackFluidCellState(fluid_it->second));
    }
  }

  if (HasChunkLight)
  {
    assert(ChunkLightCoord == chunkCoord);
    chunk->GetLightDataMutable() = ChunkLight;
  }
  if (HasChunkWeather)
  {
    assert(ChunkWeatherCoord == chunkCoord);
    chunk->SetWeatherState(ChunkWetness, ChunkWeatherLastUpdateSec);
  }

  std::array<uint8_t, CHUNK_VOLUME> *light_data = nullptr;
  for (const auto &entry : LightPacked)
  {
    const glm::ivec3 entry_chunk = UChunkManager::WorldToChunk(entry.first);
    assert(entry_chunk == chunkCoord);
    if (entry_chunk != chunkCoord)
    {
      const glm::ivec3 fallback_chunk_coord = entry_chunk;
      chunks.EnsureChunk(fallback_chunk_coord);
      UChunk *fallback_chunk = chunks.GetChunk(fallback_chunk_coord);
      if (fallback_chunk)
      {
        fallback_chunk->GetLightDataMutable()[static_cast<size_t>(
            UChunk::LocalIndex(UChunkManager::WorldToLocal(entry.first)))] =
            entry.second;
      }
      continue;
    }
    if (!light_data)
    {
      light_data = &chunk->GetLightDataMutable();
    }
    (*light_data)[static_cast<size_t>(UChunk::LocalIndex(
        UChunkManager::WorldToLocal(entry.first)))] = entry.second;
  }
}

void UChunkBuffer::Clear()
{
  Blocks.clear();
  FluidPacked.clear();
  LightPacked.clear();
  HasChunkLight = false;
  ChunkLight.fill(0);
  HasChunkWeather = false;
  ChunkWeatherCoord = glm::ivec3(0);
  ChunkWetness = 0.0f;
  ChunkWeatherLastUpdateSec = 0.0;
  HasBounds = false;
  MinY = 0;
  MaxY = -1;
  WorldGenOwnerChunk = glm::ivec3(0);
  HasWorldGenOwnerChunk = false;
  WorldGenTreeBlockRanks.clear();
}

} // namespace cutum
