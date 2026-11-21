#include "World/Chunks/ChunkBuffer.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/ChunkResidencyTrace.h"
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
                         const std::pmr::unordered_map<BlockId, int> &treeBlockRanks)
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
    const std::pmr::unordered_map<BlockId, int> &treeBlockRanks, glm::ivec3 pos,
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

UChunkBuffer::StoragePtr UChunkBuffer::CreateStorage(const Storage *source)
{
  auto owner=ActiveBufferBudget;
  if(!owner.budget && source) owner=source->Budget;
  if(owner.budget) owner.budget->Reserve(*owner.job,sizeof(Storage));
  Storage *raw=nullptr;
  try { raw=new Storage(owner); }
  catch(...) { if(owner.budget) owner.budget->Release(*owner.job,sizeof(Storage)); throw; }
  StoragePtr storage(raw);
  if(source) {
    storage->State=source->State; storage->Blocks=source->Blocks;
    storage->FluidPacked=source->FluidPacked; storage->LightPacked=source->LightPacked;
    storage->WorldGenTreeBlockRanks=source->WorldGenTreeBlockRanks;
  }
  return storage;
}

void UChunkBuffer::StorageDelete::operator()(Storage *storage) const noexcept
{
  auto owner=storage->Budget;
  delete storage;
  if(owner.budget) owner.budget->Release(*owner.job,sizeof(Storage));
}

UChunkBuffer::UChunkBuffer(const UChunkBuffer &other)
    : Payload(other.Payload ? CreateStorage(other.Payload.get()) : StoragePtr{}) {}

UChunkBuffer &UChunkBuffer::operator=(const UChunkBuffer &other)
{
  if(this!=&other) {
    auto copy=other.Payload ? CreateStorage(other.Payload.get()) : StoragePtr{};
    Payload=std::move(copy);
  }
  return *this;
}

UAllocationCountingResource::Snapshot UChunkBuffer::GetAllocationSnapshot() const
{
  return Payload ? Payload->Memory.GetSnapshot() : UAllocationCountingResource::Snapshot{};
}

size_t UChunkBuffer::GetOwnedAllocationBytes() const
{
  return Payload ? sizeof(Storage)+Payload->Memory.GetSnapshot().live_bytes : 0;
}

void UChunkBuffer::SetBlock(glm::ivec3 worldPos, BlockId id)
{
  EnsureStorage();
  Payload->State.DecodedFrameValid = false;
  if (id == BLOCK_AIR)
  {
    Payload->Blocks.erase(worldPos);
    Payload->FluidPacked.erase(worldPos);
    Payload->LightPacked.erase(worldPos);
    return;
  }
  Payload->Blocks[worldPos] = id;
  if (!Payload->State.HasBounds)
  {
    Payload->State.HasBounds = true;
    Payload->State.MinY = worldPos.y;
    Payload->State.MaxY = worldPos.y;
    return;
  }
  Payload->State.MinY = std::min(Payload->State.MinY, worldPos.y);
  Payload->State.MaxY = std::max(Payload->State.MaxY, worldPos.y);
}

void UChunkBuffer::SetFluidPacked(glm::ivec3 worldPos, uint8_t packed)
{
  EnsureStorage();
  Payload->State.DecodedFrameValid = false;
  if (packed == 0)
  {
    Payload->FluidPacked.erase(worldPos);
    return;
  }
  Payload->FluidPacked[worldPos] = packed;
}

void UChunkBuffer::SetLightPacked(glm::ivec3 worldPos, uint8_t packed)
{
  EnsureStorage();
  Payload->State.DecodedFrameValid = false;
  // Zero is an explicit light removal when applying over resident data.
  Payload->LightPacked[worldPos] = packed;
}

void UChunkBuffer::SetChunkLightData(
    glm::ivec3 chunkCoord, const std::array<uint8_t, CHUNK_VOLUME> &light)
{
  EnsureStorage();
  Payload->State.DecodedFrameValid = false;
  Payload->State.HasChunkLight = true;
  Payload->State.ChunkLightCoord = chunkCoord;
  Payload->State.ChunkLight = light;
}

void UChunkBuffer::SetChunkWeatherState(glm::ivec3 chunkCoord, float wetness,
                                        double last_update_sec)
{
  EnsureStorage();
  Payload->State.DecodedFrameValid = false;
  Payload->State.HasChunkWeather = true;
  Payload->State.ChunkWeatherCoord = chunkCoord;
  Payload->State.ChunkWetness =
      std::clamp(std::isfinite(wetness) ? wetness : 0.0f, 0.0f, 1.0f);
  Payload->State.ChunkWeatherLastUpdateSec =
      std::isfinite(last_update_sec) ? std::max(0.0, last_update_sec) : 0.0;
}

void UChunkBuffer::SetWorldGenOwnerChunk(
    glm::ivec3 chunkCoord, const UBlockRegistry &registry,
    const std::vector<std::pair<BlockId, int>> &treeBlockRanks)
{
  SetWorldGenOwnerChunk(chunkCoord,registry,treeBlockRanks.data(),treeBlockRanks.size());
}

void UChunkBuffer::SetWorldGenOwnerChunk(glm::ivec3 chunkCoord,const UBlockRegistry &registry,
    const std::pair<BlockId,int> *treeBlockRanks,size_t rankCount)
{
  EnsureStorage();
  Payload->State.DecodedFrameValid = false;
  Payload->State.WorldGenOwnerChunk = chunkCoord;
  Payload->State.HasWorldGenOwnerChunk = true;
  Payload->WorldGenTreeBlockRanks.clear();
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
      Payload->WorldGenTreeBlockRanks[id] = rank;
    }
  }
  for (size_t index=0;index<rankCount;++index)
  {
    const auto &[id,rank]=treeBlockRanks[index];
    if (id != BLOCK_AIR && rank > 0)
    {
      Payload->WorldGenTreeBlockRanks[id] = std::max(Payload->WorldGenTreeBlockRanks[id], rank);
    }
  }
}

BlockId UChunkBuffer::GetBlock(glm::ivec3 worldPos) const
{
  if(!Payload) return BLOCK_AIR;
  const auto it = Payload->Blocks.find(worldPos);
  if (it == Payload->Blocks.end())
  {
    return BLOCK_AIR;
  }
  return it->second;
}

uint8_t UChunkBuffer::GetFluidPacked(glm::ivec3 worldPos) const
{
  if(!Payload) return 0;
  const auto it = Payload->FluidPacked.find(worldPos);
  if (it == Payload->FluidPacked.end())
  {
    return 0;
  }
  return it->second;
}

uint8_t UChunkBuffer::GetLightPacked(glm::ivec3 worldPos) const
{
  if(!Payload) return 0;
  if (Payload->State.HasChunkLight)
  {
    const glm::ivec3 local = worldPos - Payload->State.ChunkLightCoord * CHUNK_SIZE;
    if (local.x >= 0 && local.x < CHUNK_SIZE && local.y >= 0 &&
        local.y < CHUNK_SIZE && local.z >= 0 && local.z < CHUNK_SIZE)
    {
      const int index = local.x + CHUNK_SIZE * (local.y + CHUNK_SIZE * local.z);
      return Payload->State.ChunkLight[static_cast<size_t>(index)];
    }
  }
  const auto it = Payload->LightPacked.find(worldPos);
  if (it == Payload->LightPacked.end())
  {
    return 0;
  }
  return it->second;
}

void UChunkBuffer::ApplyTo(UBlockWorld &world) const
{
  if(!Payload) return;
  for (const auto &entry : Payload->Blocks)
  {
    const bool ownerChunk =
        !Payload->State.HasWorldGenOwnerChunk ||
        UChunkManager::WorldToChunk(entry.first) == Payload->State.WorldGenOwnerChunk;
    const int incomingTreeRank =
        TreeFeatureBlockRank(entry.second, Payload->WorldGenTreeBlockRanks);
    const int currentTreeRank = TreeFeatureBlockRank(
        world.GetBlock(entry.first), Payload->WorldGenTreeBlockRanks);
    if ((Payload->State.HasWorldGenOwnerChunk && !ownerChunk &&
         !ShouldApplyCrossChunkBlock(world, Payload->WorldGenTreeBlockRanks, entry.first,
                                     entry.second)) ||
        (ownerChunk && incomingTreeRank > 0 && currentTreeRank > 0 &&
         !ShouldApplyCrossChunkBlock(world, Payload->WorldGenTreeBlockRanks, entry.first,
                                     entry.second)))
    {
      continue;
    }
    const auto target = UChunkManager::WorldToChunk(entry.first);
    if (TraceChunkResidencyEnabled(target) &&
        !world.GetChunkManager().HasChunk(target))
      TraceChunkResidency(
          Payload->State.HasWorldGenOwnerChunk ? "worldgen_buffer_missing" : "buffer_missing",
          target, Payload->State.WorldGenOwnerChunk, entry.second, &world.GetChunkManager());
    world.SetBlock(entry.first, entry.second);
    const auto fluid_it = Payload->FluidPacked.find(entry.first);
    if (fluid_it != Payload->FluidPacked.end() && fluid_it->second != 0)
    {
      world.SetFluidState(entry.first, UnpackFluidCellState(fluid_it->second));
    }
  }
  if (Payload->State.HasChunkLight)
  {
    world.GetChunkManager().EnsureChunk(Payload->State.ChunkLightCoord);
    if (UChunk *chunk = world.GetChunkManager().GetChunk(Payload->State.ChunkLightCoord))
    {
      chunk->ReplaceLightData(Payload->State.ChunkLight);
    }
  }
  if (Payload->State.HasChunkWeather)
  {
    world.GetChunkManager().EnsureChunk(Payload->State.ChunkWeatherCoord);
    if (UChunk *chunk = world.GetChunkManager().GetChunk(Payload->State.ChunkWeatherCoord))
    {
      chunk->SetWeatherState(Payload->State.ChunkWetness, Payload->State.ChunkWeatherLastUpdateSec);
    }
  }
  for (const auto &entry : Payload->LightPacked)
  {
    const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(entry.first);
    world.GetChunkManager().EnsureChunk(chunk_coord);
    UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
    if (!chunk)
    {
      continue;
    }
    const glm::ivec3 local = UChunkManager::WorldToLocal(entry.first);
    chunk->SetLightLocal(local, entry.second & 0x0F, entry.second >> 4);
  }
}

void UChunkBuffer::ApplyToChunk(UBlockWorld &world, glm::ivec3 chunkCoord) const
{
  if (IsEmpty())
  {
    return;
  }

  UChunkManager &chunks = world.GetChunkManager();
  // A geometry-free AIR owner is a complete empty base too. Existing geometry
  // may contain an unvalidated partial generation or local edits, so applying
  // a sparse decoded overlay there cannot mint new full-frame authority.
  const bool fresh_target = !chunks.HasChunk(chunkCoord);
  chunks.EnsureChunk(chunkCoord);
  UChunk *chunk = chunks.GetChunk(chunkCoord);
  if (!chunk)
  {
    return;
  }

  for (const auto &entry : Payload->Blocks)
  {
    assert(UChunkManager::WorldToChunk(entry.first) == chunkCoord);
    world.SetBlockInChunk(*chunk, entry.first, entry.second);
    const auto fluid_it = Payload->FluidPacked.find(entry.first);
    if (fluid_it != Payload->FluidPacked.end() && fluid_it->second != 0)
    {
      world.SetFluidStateInChunk(*chunk, entry.first,
                                 UnpackFluidCellState(fluid_it->second));
    }
  }

  if (Payload->State.HasChunkLight)
  {
    assert(Payload->State.ChunkLightCoord == chunkCoord);
    chunk->ReplaceLightData(Payload->State.ChunkLight);
  }
  if (Payload->State.HasChunkWeather)
  {
    assert(Payload->State.ChunkWeatherCoord == chunkCoord);
    chunk->SetWeatherState(Payload->State.ChunkWetness, Payload->State.ChunkWeatherLastUpdateSec);
  }

  for (const auto &entry : Payload->LightPacked)
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
        fallback_chunk->SetLightLocal(UChunkManager::WorldToLocal(entry.first),
                                      entry.second & 0x0F, entry.second >> 4);
      }
      continue;
    }
    chunk->SetLightLocal(UChunkManager::WorldToLocal(entry.first),
                         entry.second & 0x0F, entry.second >> 4);
  }
  if (Payload->State.DecodedFrameValid && Payload->State.DecodedCoord == chunkCoord && fresh_target &&
      !Payload->State.HasWorldGenOwnerChunk && HasGeometryPayload())
    chunks.NoteDecodedTerrainSlice(chunkCoord);
}

std::optional<LoadedAirSlice>
UChunkBuffer::GetDecodedAirSlice(glm::ivec3 coord) const
{
  if(!Payload) return std::nullopt;
  if (!Payload->State.DecodedFrameValid || Payload->State.DecodedCoord != coord || !Payload->Blocks.empty() ||
      !Payload->FluidPacked.empty() || !Payload->LightPacked.empty() || Payload->State.HasWorldGenOwnerChunk ||
      (Payload->State.HasChunkLight && Payload->State.ChunkLightCoord != coord) ||
      (Payload->State.HasChunkWeather && Payload->State.ChunkWeatherCoord != coord))
    return std::nullopt;
  LoadedAirSlice result;
  result.Coord = coord;
  result.ExplicitDecode = true;
  if (Payload->State.HasChunkLight)
    result.LegacyLight =
        std::make_shared<const std::array<uint8_t, CHUNK_VOLUME>>(Payload->State.ChunkLight);
  result.HasWeather = Payload->State.HasChunkWeather;
  result.Wetness = Payload->State.ChunkWetness;
  result.WeatherLastUpdateSec = Payload->State.ChunkWeatherLastUpdateSec;
  return result;
}

void UChunkBuffer::Clear()
{
  Payload.reset();
}

} // namespace cutum
