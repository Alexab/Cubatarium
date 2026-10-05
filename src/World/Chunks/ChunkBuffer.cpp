#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include <algorithm>
#include <cassert>

namespace cutum
{

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
      const int index =
          local.x + CHUNK_SIZE * (local.y + CHUNK_SIZE * local.z);
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
    world.SetBlock(entry.first, entry.second);
    const auto fluid_it = FluidPacked.find(entry.first);
    if (fluid_it != FluidPacked.end() && fluid_it->second != 0)
    {
      world.SetFluidState(entry.first,
                         UnpackFluidCellState(fluid_it->second));
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
    chunk->GetLightDataMutable()[static_cast<size_t>(UChunk::LocalIndex(local))] =
        entry.second;
  }
}

void UChunkBuffer::ApplyToChunk(UBlockWorld &world,
                                glm::ivec3 chunkCoord) const
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
      world.SetFluidStateInChunk(
          *chunk, entry.first, UnpackFluidCellState(fluid_it->second));
    }
  }

  if (HasChunkLight)
  {
    assert(ChunkLightCoord == chunkCoord);
    chunk->GetLightDataMutable() = ChunkLight;
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
  HasBounds = false;
  MinY = 0;
  MaxY = -1;
}

} // namespace cutum
