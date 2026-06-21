#include "World/Chunks/ChunkStreamer.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

bool ChunkAabbIntersectsPlayer(glm::ivec3 chunkCoord, const glm::vec3 &eyePos,
                               const PlayerCapsule &cap)
{
  const float feetY = cap.feetY(eyePos);
  const float px0 = eyePos.x - cap.halfWidth;
  const float px1 = eyePos.x + cap.halfWidth;
  const float py0 = feetY;
  const float py1 = eyePos.y;
  const float pz0 = eyePos.z - cap.halfWidth;
  const float pz1 = eyePos.z + cap.halfWidth;

  const float cx0 = static_cast<float>(chunkCoord.x * CHUNK_SIZE) - 0.5f;
  const float cx1 = static_cast<float>((chunkCoord.x + 1) * CHUNK_SIZE) - 0.5f;
  const float cy0 = static_cast<float>(chunkCoord.y * CHUNK_SIZE) - 0.5f;
  const float cy1 = static_cast<float>((chunkCoord.y + 1) * CHUNK_SIZE) - 0.5f;
  const float cz0 = static_cast<float>(chunkCoord.z * CHUNK_SIZE) - 0.5f;
  const float cz1 = static_cast<float>((chunkCoord.z + 1) * CHUNK_SIZE) - 0.5f;

  return px0 <= cx1 && px1 >= cx0 && py0 <= cy1 && py1 >= cy0 && pz0 <= cz1 &&
         pz1 >= cz0;
}

bool TerrainColumnIsEmpty(const UBlockWorld &world, int worldX, int worldZ)
{
  return world.GetBlock(glm::ivec3(worldX, 0, worldZ)) == BLOCK_AIR;
}

void MarkChunkAndNeighborsDirty(const UChunkStreamer::MarkDirtyFn &markDirtyFn,
                                glm::ivec3 chunkCoord)
{
  if (!markDirtyFn)
  {
    return;
  }
  markDirtyFn(chunkCoord);
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    markDirtyFn(chunkCoord + offset);
  }
}

} // namespace

UChunkStreamer::UChunkStreamer(UBlockWorld &world, UBlockRegistry &registry,
                               uint32_t Seed, int baseY, int MaxHeight)
    : World(world), Registry(registry), Seed(Seed), BaseY(baseY),
      MaxHeight(MaxHeight)
{
}

void UChunkStreamer::SetWorldFolder(const std::string &path)
{
  WorldFolder = path;
}

void UChunkStreamer::SetCallbacks(LoadChunkFn loadFn, SaveChunkFn saveFn,
                                  MarkDirtyFn markDirtyFn,
                                  GenerateColumnFn generateColumnFn,
                                  UnloadChunkFn unloadFn)
{
  OnLoadChunk = std::move(loadFn);
  OnSaveChunk = std::move(saveFn);
  OnMarkDirty = std::move(markDirtyFn);
  OnGenerateColumn = std::move(generateColumnFn);
  OnUnloadChunk = std::move(unloadFn);
}

void UChunkStreamer::SetAsyncCallbacks(RequestAsyncChunkFn requestFn,
                                       IsChunkCommittedFn isCommittedFn)
{
  OnRequestAsyncChunk = std::move(requestFn);
  OnIsChunkCommitted = std::move(isCommittedFn);
}

void UChunkStreamer::NotifyChunkCommitted(glm::ivec3 chunkCoord)
{
  ProcedurallyGenerated.insert(chunkCoord);
  MarkChunkAndNeighborsDirty(OnMarkDirty, chunkCoord);
}

bool UChunkStreamer::EnsureChunkLoaded(glm::ivec3 chunkCoord, bool forceSync)
{
  // UTerrain columns are generated at world Y=0..surface; only fill ground
  // layer chunks.
  if (chunkCoord.y != 0)
  {
    return false;
  }

  const UChunk *existing = World.GetChunkManager().GetChunk(chunkCoord);
  if (existing != nullptr && !forceSync && AsyncGeneration &&
      OnIsChunkCommitted && OnIsChunkCommitted(chunkCoord))
  {
    ProcedurallyGenerated.insert(chunkCoord);
    return true;
  }
  if (existing != nullptr && !OnGenerateColumn && !AsyncGeneration)
  {
    return false;
  }

  if (existing == nullptr)
  {
    if (OnLoadChunk && OnLoadChunk(chunkCoord))
    {
      ProcedurallyGenerated.insert(chunkCoord);
      MarkChunkAndNeighborsDirty(OnMarkDirty, chunkCoord);
      return true;
    }
    if (AsyncGeneration && !forceSync && OnRequestAsyncChunk)
    {
      const int priority =
          std::abs(chunkCoord.x) + std::abs(chunkCoord.z);
      OnRequestAsyncChunk(chunkCoord, priority);
      return OnIsChunkCommitted && OnIsChunkCommitted(chunkCoord);
    }
    if (!OnGenerateColumn)
    {
      return false;
    }
  }
  else if (!OnGenerateColumn)
  {
    if (AsyncGeneration && !forceSync && OnRequestAsyncChunk)
    {
      OnRequestAsyncChunk(chunkCoord, 0);
      return OnIsChunkCommitted && OnIsChunkCommitted(chunkCoord);
    }
    return false;
  }

  bool generated = false;
  const bool onlyEmptyColumns = existing != nullptr;
  for (int lx = 0; lx < CHUNK_SIZE; ++lx)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      const int worldX = chunkCoord.x * CHUNK_SIZE + lx;
      const int worldZ = chunkCoord.z * CHUNK_SIZE + lz;
      if (onlyEmptyColumns && !TerrainColumnIsEmpty(World, worldX, worldZ))
      {
        continue;
      }
      OnGenerateColumn(worldX, worldZ);
      generated = true;
    }
  }

  if (!generated)
  {
    if (existing != nullptr)
    {
      ProcedurallyGenerated.insert(chunkCoord);
    }
    return false;
  }

  MarkChunkAndNeighborsDirty(OnMarkDirty, chunkCoord);
  ProcedurallyGenerated.insert(chunkCoord);
  return true;
}

bool UChunkStreamer::ShouldKeepChunkLoaded(glm::ivec3 chunkCoord,
                                           glm::ivec3 feetBlockPos,
                                           const glm::vec3 &eyePos,
                                           const PlayerCapsule &cap) const
{
  const glm::ivec3 feetChunk = UChunkManager::WorldToChunk(feetBlockPos);

  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dy = -1; dy <= 1; ++dy)
    {
      for (int dz = -1; dz <= 1; ++dz)
      {
        if (chunkCoord == feetChunk + glm::ivec3(dx, dy, dz))
        {
          return true;
        }
      }
    }
  }

  const glm::ivec3 playerChunk = UChunkManager::WorldToChunk(feetBlockPos);
  const int dx = std::abs(chunkCoord.x - playerChunk.x);
  const int dy = std::abs(chunkCoord.y - playerChunk.y);
  const int dz = std::abs(chunkCoord.z - playerChunk.z);
  const int distFromPlayer = std::max({dx, dy, dz});
  if (distFromPlayer <= RenderDistance + UnloadMargin)
  {
    return true;
  }

  if (ChunkAabbIntersectsPlayer(chunkCoord, eyePos, cap))
  {
    return true;
  }

  return false;
}

void UChunkStreamer::EnsureCollisionChunks(glm::ivec3 feetBlockPos)
{
  if (!Enabled)
  {
    return;
  }

  const glm::ivec3 feetChunk = UChunkManager::WorldToChunk(feetBlockPos);
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int dy = -1; dy <= 1; ++dy)
      {
        const glm::ivec3 coord = feetChunk + glm::ivec3(dx, dy, dz);
        EnsureChunkLoaded(coord, true);
      }
    }
  }
}

void UChunkStreamer::UnloadDistantChunks(glm::ivec3 centerChunk,
                                         glm::ivec3 feetBlockPos,
                                         const glm::vec3 &eyePos,
                                         const PlayerCapsule &cap)
{
  std::vector<glm::ivec3> toUnload;
  const int limit = RenderDistance + UnloadMargin;

  World.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      {
        const glm::ivec3 coord = chunk.GetCoord();
        const int dx = std::abs(coord.x - centerChunk.x);
        const int dy = std::abs(coord.y - centerChunk.y);
        const int dz = std::abs(coord.z - centerChunk.z);
        const int dist = std::max({dx, dy, dz});
        if (dist > limit)
        {
          toUnload.push_back(coord);
        }
      });

  int unloadOps = 0;
  for (const glm::ivec3 &coord : toUnload)
  {
    if (unloadOps >= MaxUnloadOpsPerFrame)
    {
      break;
    }
    if (ShouldKeepChunkLoaded(coord, feetBlockPos, eyePos, cap))
    {
      continue;
    }
    if (OnSaveChunk)
    {
      OnSaveChunk(coord);
      ++LastFrameStats.savesThisFrame;
    }
    World.GetChunkManager().RemoveChunk(coord);
    ProcedurallyGenerated.erase(coord);
    if (OnUnloadChunk)
    {
      OnUnloadChunk(coord);
    }
    LastFrameStats.unloadedCoords.push_back(coord);
    ++LastFrameStats.unloadsThisFrame;
    ++unloadOps;
  }
}

void UChunkStreamer::Update(glm::ivec3 cameraBlockPos, const glm::vec3 &eyePos,
                            const PlayerCapsule &cap)
{
  if (!Enabled)
  {
    return;
  }

  LastFrameStats.Reset();

  const glm::ivec3 centerChunk = UChunkManager::WorldToChunk(cameraBlockPos);
  const glm::ivec3 feetBlockPos =
      WorldPosToBlock(glm::vec3(eyePos.x, cap.feetY(eyePos) + 0.01f, eyePos.z));

  int loadOps = 0;
  for (int cx = centerChunk.x - RenderDistance;
       cx <= centerChunk.x + RenderDistance; ++cx)
  {
    for (int cz = centerChunk.z - RenderDistance;
         cz <= centerChunk.z + RenderDistance; ++cz)
    {
      if (loadOps >= MaxLoadOpsPerFrame)
      {
        goto finish_loads;
      }
      const glm::ivec3 coord(cx, 0, cz);
      if (ProcedurallyGenerated.count(coord))
      {
        continue;
      }
      if (EnsureChunkLoaded(coord))
      {
        LastFrameStats.loadedCoords.push_back(coord);
        ++LastFrameStats.loadsThisFrame;
        ++loadOps;
      }
    }
  }
finish_loads:

  UnloadDistantChunks(centerChunk, feetBlockPos, eyePos, cap);
}

} // namespace cutum
