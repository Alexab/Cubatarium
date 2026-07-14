#include "World/Core/BlockCountTracker.h"

#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"

namespace cutum
{

namespace
{

size_t CountChunkNonAir(const UChunk &chunk)
{
  size_t n = 0;
  for (int z = 0; z < CHUNK_SIZE; ++z)
  {
    for (int y = 0; y < CHUNK_SIZE; ++y)
    {
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        if (chunk.GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
        {
          ++n;
        }
      }
    }
  }
  return n;
}

} // namespace

void UBlockCountTracker::Reset(size_t value)
{
  Count = value;
  NeedsRecountFlag = false;
  RecountQueue.clear();
  RecountIndex = 0;
}

void UBlockCountTracker::OnBlockChanged(bool was_air, bool is_air)
{
  if (was_air == is_air)
  {
    return;
  }
  if (was_air && !is_air)
  {
    ++Count;
  }
  else if (!was_air && is_air)
  {
    if (Count > 0)
    {
      --Count;
    }
  }
}

void UBlockCountTracker::MarkNeedsRecount()
{
  NeedsRecountFlag = true;
  RecountQueue.clear();
  RecountIndex = 0;
  Count = 0;
}

void UBlockCountTracker::TickRecount(const UBlockWorld &world, int max_chunks)
{
  if (!NeedsRecountFlag)
  {
    return;
  }
  if (RecountQueue.empty())
  {
    world.GetChunkManager().ForEachChunk(
        [&](const UChunk &chunk)
        { RecountQueue.push_back(chunk.GetCoord()); });
    RecountIndex = 0;
    Count = 0;
  }
  int processed = 0;
  while (RecountIndex < RecountQueue.size() && processed < max_chunks)
  {
    const glm::ivec3 coord = RecountQueue[RecountIndex++];
    const UChunk *chunk = world.GetChunkManager().GetChunk(coord);
    if (chunk)
    {
      Count += CountChunkNonAir(*chunk);
    }
    ++processed;
  }
  if (RecountIndex >= RecountQueue.size())
  {
    NeedsRecountFlag = false;
    RecountQueue.clear();
    RecountIndex = 0;
  }
}

} // namespace cutum
