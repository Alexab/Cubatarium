#include "World/Chunks/ChunkStreamer.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/TerrainColumnUtil.h"
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

bool TerrainColumnNeedsGeneration(const UBlockWorld &world, int worldX,
                                  int worldZ, int maxWorldY)
{
  return TerrainColumnNeedsFill(world, worldX, worldZ, maxWorldY);
}

void MarkTerrainColumnMeshDirty(const UChunkStreamer::MarkDirtyFn &markDirtyFn,
                                glm::ivec3 groundCoord)
{
  if (!markDirtyFn)
  {
    return;
  }
  groundCoord.y = 0;
  markDirtyFn(groundCoord);
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
  MarkTerrainColumnMeshDirty(OnMarkDirty, chunkCoord);
}

void UChunkStreamer::MarkPersistedColumnsFromWorld()
{
  std::unordered_set<glm::ivec3, IVec3Hash> columns;
  World.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      {
        const glm::ivec3 coord = chunk.GetCoord();
        columns.insert(glm::ivec3(coord.x, 0, coord.z));
      });
  for (const glm::ivec3 &ground : columns)
  {
    if (IsTerrainChunkComplete(World, ground, MaxHeight))
    {
      ProcedurallyGenerated.insert(ground);
    }
  }
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
  bool clearedPartialDiskLoad = false;
  if (existing != nullptr &&
      IsTerrainChunkComplete(World, chunkCoord, MaxHeight))
  {
    ProcedurallyGenerated.insert(chunkCoord);
    return true;
  }
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
      if (IsTerrainChunkComplete(World, chunkCoord, MaxHeight))
      {
        ProcedurallyGenerated.insert(chunkCoord);
        MarkTerrainColumnMeshDirty(OnMarkDirty, chunkCoord);
        return true;
      }
      ClearTerrainColumnChunks(World, chunkCoord, MaxHeight);
      clearedPartialDiskLoad = true;
    }
    if (AsyncGeneration && !forceSync && OnRequestAsyncChunk)
    {
      const int priority =
          std::abs(chunkCoord.x) + std::abs(chunkCoord.z);
      OnRequestAsyncChunk(chunkCoord, priority);
      return OnIsChunkCommitted && OnIsChunkCommitted(chunkCoord) &&
             IsTerrainChunkComplete(World, chunkCoord, MaxHeight);
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
      return OnIsChunkCommitted && OnIsChunkCommitted(chunkCoord) &&
             IsTerrainChunkComplete(World, chunkCoord, MaxHeight);
    }
    return false;
  }

  bool generated = false;
  const bool onlyEmptyColumns = existing != nullptr && !clearedPartialDiskLoad;
  for (int lx = 0; lx < CHUNK_SIZE; ++lx)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      const int worldX = chunkCoord.x * CHUNK_SIZE + lx;
      const int worldZ = chunkCoord.z * CHUNK_SIZE + lz;
      if (onlyEmptyColumns &&
          !TerrainColumnNeedsGeneration(World, worldX, worldZ, MaxHeight))
      {
        continue;
      }
      OnGenerateColumn(worldX, worldZ);
      generated = true;
    }
  }

  if (!generated)
  {
    if (existing != nullptr &&
        IsTerrainChunkComplete(World, chunkCoord, MaxHeight))
    {
      ProcedurallyGenerated.insert(chunkCoord);
    }
    return false;
  }

  MarkTerrainColumnMeshDirty(OnMarkDirty, chunkCoord);
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

  const int dx = std::abs(chunkCoord.x - feetChunk.x);
  const int dz = std::abs(chunkCoord.z - feetChunk.z);
  const int horizDist = std::max(dx, dz);
  if (horizDist <= RenderDistance + UnloadMargin)
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
  const glm::ivec3 groundCenter(feetChunk.x, 0, feetChunk.z);

  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      if (dx == 0 && dz == 0)
      {
        continue;
      }
      const glm::ivec3 coord(groundCenter.x + dx, 0, groundCenter.z + dz);
      EnsureChunkLoaded(coord, false);
    }
  }

  const bool centerReady = EnsureChunkLoaded(groundCenter, false);
  if (!centerReady && OnGenerateColumn)
  {
    EnsureChunkLoaded(groundCenter, true);
  }
}

void UChunkStreamer::UnloadDistantChunks(glm::ivec3 /*centerChunk*/,
                                         glm::ivec3 feetBlockPos,
                                         const glm::vec3 &eyePos,
                                         const PlayerCapsule &cap)
{
  const glm::ivec3 feetChunk = UChunkManager::WorldToChunk(feetBlockPos);
  const int limit = RenderDistance + UnloadMargin;
  const int maxCy = (MaxHeight + CHUNK_SIZE - 1) / CHUNK_SIZE;

  std::unordered_set<glm::ivec3, IVec3Hash> columnsInMemory;
  World.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      {
        const glm::ivec3 coord = chunk.GetCoord();
        columnsInMemory.insert(glm::ivec3(coord.x, 0, coord.z));
      });

  int unloadOps = 0;
  std::unordered_set<glm::ivec3, IVec3Hash> savedColumns;
  for (const glm::ivec3 &ground : columnsInMemory)
  {
    if (unloadOps >= MaxUnloadOpsPerFrame)
    {
      break;
    }

    const int dx = std::abs(ground.x - feetChunk.x);
    const int dz = std::abs(ground.z - feetChunk.z);
    if (std::max(dx, dz) <= limit)
    {
      continue;
    }

    bool keepColumn = false;
    for (int cy = 0; cy <= maxCy; ++cy)
    {
      const glm::ivec3 slice(ground.x, cy, ground.z);
      if (!World.GetChunkManager().HasChunk(slice))
      {
        continue;
      }
      if (ShouldKeepChunkLoaded(slice, feetBlockPos, eyePos, cap))
      {
        keepColumn = true;
        break;
      }
    }
    if (keepColumn)
    {
      continue;
    }

    if (OnSaveChunk && savedColumns.insert(ground).second)
    {
      OnSaveChunk(ground);
      ++LastFrameStats.savesThisFrame;
    }

    for (int cy = 0; cy <= maxCy; ++cy)
    {
      const glm::ivec3 slice(ground.x, cy, ground.z);
      if (!World.GetChunkManager().HasChunk(slice))
      {
        continue;
      }
      World.GetChunkManager().RemoveChunk(slice);
      if (OnUnloadChunk)
      {
        OnUnloadChunk(slice);
      }
      LastFrameStats.unloadedCoords.push_back(slice);
      ++LastFrameStats.unloadsThisFrame;
    }
    ProcedurallyGenerated.erase(ground);
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
