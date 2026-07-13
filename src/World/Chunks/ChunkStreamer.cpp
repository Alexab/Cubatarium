#include "World/Chunks/ChunkStreamer.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkLoadPriority.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"
#include "World/Physics/CollisionReadiness.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

constexpr int kCollisionSyncSubColumnsPerFrame = 32;
constexpr int kTerrainSubColumnsPerChunk = CHUNK_SIZE * CHUNK_SIZE;

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

void UChunkStreamer::SetColumnPendingCallback(IsColumnPendingFn fn)
{
  OnIsColumnPending = std::move(fn);
}

void UChunkStreamer::SetGenerationLightingHooks(
    std::function<void(bool)> defer_relight,
    std::function<void(glm::ivec3)> relight_column)
{
  OnSetLightingRelightDeferred = std::move(defer_relight);
  OnRelightTerrainColumn = std::move(relight_column);
}

void UChunkStreamer::NotifyChunkCommitted(glm::ivec3 chunkCoord)
{
  glm::ivec3 ground(chunkCoord.x, 0, chunkCoord.z);
  InvalidateTerrainCompleteCache(ground);
  ProcedurallyGenerated.insert(ground);
}

bool UChunkStreamer::IsTerrainChunkCompleteCached(glm::ivec3 groundCoord)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const auto cached = TerrainCompleteCache.find(groundCoord);
  if (cached != TerrainCompleteCache.end())
  {
    return cached->second;
  }
  const bool complete =
      IsTerrainChunkComplete(World, groundCoord, MaxHeight);
  TerrainCompleteCache.emplace(groundCoord, complete);
  return complete;
}

void UChunkStreamer::InvalidateTerrainCompleteCache(glm::ivec3 groundCoord)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  TerrainCompleteCache.erase(groundCoord);
  ColumnGenStates.erase(groundCoord);
}

int UChunkStreamer::ChunkHorizontalDistance(glm::ivec3 groundCoord) const
{
  return ChunkChebyshevDistance(groundCoord, LoadPriorityCenter);
}

int UChunkStreamer::ChunkLoadPriorityFor(glm::ivec3 groundCoord) const
{
  int priority = ComputeChunkLoadPriority(groundCoord, LoadPriorityCenter, ViewForwardXz,
                                          PriorityParams);
  if (CollisionUrgent)
  {
    const int dist = std::max(
        {std::abs(groundCoord.x - CollisionUrgentCenter.x),
         std::abs(groundCoord.y - CollisionUrgentCenter.y),
         std::abs(groundCoord.z - CollisionUrgentCenter.z)});
    if (dist <= CollisionUrgentRadius)
    {
      priority += 10000;
    }
  }
  return priority;
}

void UChunkStreamer::SetCollisionUrgentRing(glm::ivec3 feet_chunk, int radius_chunks,
                                            bool urgent)
{
  CollisionUrgent = urgent;
  CollisionUrgentCenter = feet_chunk;
  CollisionUrgentRadius = radius_chunks;
}

bool UChunkStreamer::RingPrerequisitesMet(glm::ivec3 coord)
{
  if (!RingGateEnabled)
  {
    return true;
  }
  const int ring = ChunkHorizontalDistance(coord);
  if (ring <= 1)
  {
    return true;
  }
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      if (dx == 0 && dz == 0)
      {
        continue;
      }
      const glm::ivec3 neighbor(coord.x + dx, 0, coord.z + dz);
      const int neighbor_ring = ChunkHorizontalDistance(neighbor);
      if (neighbor_ring >= ring)
      {
        continue;
      }
      if (!ProcedurallyGenerated.count(neighbor) ||
          !IsTerrainChunkCompleteCached(neighbor))
      {
        if (OnIsChunkCommitted && !OnIsChunkCommitted(neighbor))
        {
          return false;
        }
        if (!ProcedurallyGenerated.count(neighbor) ||
            !IsTerrainChunkCompleteCached(neighbor))
        {
          return false;
        }
      }
    }
  }
  return true;
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
    if (IsTerrainChunkCompleteCached(ground))
    {
      ProcedurallyGenerated.insert(ground);
    }
  }
}

bool UChunkStreamer::AdvanceTerrainColumnGeneration(glm::ivec3 chunkCoord,
                                                    int max_sub_columns,
                                                    bool only_empty_columns)
{
  if (!OnGenerateColumn || max_sub_columns <= 0)
  {
    return IsTerrainChunkCompleteCached(chunkCoord);
  }

  ColumnGenState &state = ColumnGenStates[chunkCoord];
  state.onlyEmptyColumns = only_empty_columns;

  bool generated = false;
  int generated_this_call = 0;

  if (OnSetLightingRelightDeferred)
  {
    OnSetLightingRelightDeferred(true);
  }

  while (state.cursor < kTerrainSubColumnsPerChunk &&
         generated_this_call < max_sub_columns)
  {
    const int lx = state.cursor % CHUNK_SIZE;
    const int lz = state.cursor / CHUNK_SIZE;
    ++state.cursor;
    const int worldX = chunkCoord.x * CHUNK_SIZE + lx;
    const int worldZ = chunkCoord.z * CHUNK_SIZE + lz;
    if (state.onlyEmptyColumns &&
        !TerrainColumnNeedsGeneration(World, worldX, worldZ, MaxHeight))
    {
      continue;
    }
    OnGenerateColumn(worldX, worldZ);
    generated = true;
    ++generated_this_call;
  }

  if (OnSetLightingRelightDeferred)
  {
    OnSetLightingRelightDeferred(false);
  }

  if (!generated && state.cursor >= kTerrainSubColumnsPerChunk)
  {
    ColumnGenStates.erase(chunkCoord);
    const UChunk *existing = World.GetChunkManager().GetChunk(chunkCoord);
    if (existing != nullptr && IsTerrainChunkCompleteCached(chunkCoord))
    {
      ProcedurallyGenerated.insert(chunkCoord);
    }
    return IsTerrainChunkCompleteCached(chunkCoord);
  }

  if (!generated)
  {
    return IsTerrainChunkCompleteCached(chunkCoord);
  }

  const bool complete = IsTerrainChunkCompleteCached(chunkCoord);
  if (complete)
  {
    ColumnGenStates.erase(chunkCoord);
    if (OnRelightTerrainColumn)
    {
      OnRelightTerrainColumn(chunkCoord);
    }
    MarkTerrainColumnMeshDirty(OnMarkDirty, chunkCoord);
    ProcedurallyGenerated.insert(chunkCoord);
  }
  else
  {
    ProcedurallyGenerated.erase(chunkCoord);
  }
  return complete;
}

bool UChunkStreamer::EnsureChunkLoaded(glm::ivec3 chunkCoord, bool forceSync)
{
  // UTerrain columns are generated at world Y=0..surface; only fill ground
  // layer chunks.
  if (chunkCoord.y != 0)
  {
    return false;
  }

  if (OnIsColumnPending && OnIsColumnPending(chunkCoord))
  {
    return IsTerrainChunkCompleteCached(chunkCoord);
  }

  const UChunk *existing = World.GetChunkManager().GetChunk(chunkCoord);
  bool clearedPartialDiskLoad = false;
  if (existing != nullptr &&
      IsTerrainChunkCompleteCached(chunkCoord))
  {
    ProcedurallyGenerated.insert(chunkCoord);
    return true;
  }
  if (existing != nullptr && !forceSync && AsyncGeneration &&
      OnIsChunkCommitted && OnIsChunkCommitted(chunkCoord))
  {
    if (IsTerrainChunkCompleteCached(chunkCoord))
    {
      ProcedurallyGenerated.insert(chunkCoord);
      return true;
    }
    ClearTerrainColumnChunks(World, chunkCoord, MaxHeight);
    InvalidateTerrainCompleteCache(chunkCoord);
    if (OnRequestAsyncChunk)
    {
      OnRequestAsyncChunk(chunkCoord, ChunkLoadPriorityFor(chunkCoord));
    }
    return false;
  }
  if (existing != nullptr && !OnGenerateColumn && !AsyncGeneration)
  {
    return false;
  }

  if (existing == nullptr)
  {
    if (OnLoadChunk && OnLoadChunk(chunkCoord))
    {
      if (IsTerrainChunkCompleteCached(chunkCoord))
      {
        ProcedurallyGenerated.insert(chunkCoord);
        MarkTerrainColumnMeshDirty(OnMarkDirty, chunkCoord);
        return true;
      }
      ClearTerrainColumnChunks(World, chunkCoord, MaxHeight);
      InvalidateTerrainCompleteCache(chunkCoord);
      clearedPartialDiskLoad = true;
    }
    if (AsyncGeneration && !forceSync && OnRequestAsyncChunk)
    {
      const int priority = ChunkLoadPriorityFor(chunkCoord);
      OnRequestAsyncChunk(chunkCoord, priority);
      return OnIsChunkCommitted && OnIsChunkCommitted(chunkCoord) &&
             IsTerrainChunkCompleteCached(chunkCoord);
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
      OnRequestAsyncChunk(chunkCoord, ChunkLoadPriorityFor(chunkCoord));
      return OnIsChunkCommitted && OnIsChunkCommitted(chunkCoord) &&
             IsTerrainChunkCompleteCached(chunkCoord);
    }
    return false;
  }

  if (existing != nullptr && !clearedPartialDiskLoad && !forceSync &&
      AsyncGeneration && OnRequestAsyncChunk &&
      !IsTerrainChunkCompleteCached(chunkCoord))
  {
    ClearTerrainColumnChunks(World, chunkCoord, MaxHeight);
    InvalidateTerrainCompleteCache(chunkCoord);
    ColumnGenStates.erase(chunkCoord);
    OnRequestAsyncChunk(chunkCoord, ChunkLoadPriorityFor(chunkCoord));
    return OnIsChunkCommitted && OnIsChunkCommitted(chunkCoord) &&
           IsTerrainChunkCompleteCached(chunkCoord);
  }

  const bool onlyEmptyColumns = existing != nullptr && !clearedPartialDiskLoad;
  const int sub_column_budget =
      forceSync ? kCollisionSyncSubColumnsPerFrame : kTerrainSubColumnsPerChunk;
  return AdvanceTerrainColumnGeneration(chunkCoord, sub_column_budget,
                                        onlyEmptyColumns);
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

bool UChunkStreamer::IsPositionInActiveRing(const glm::vec3 &worldPos,
                                            glm::ivec3 feetBlockPos,
                                            const glm::vec3 &eyePos,
                                            const PlayerCapsule &cap) const
{
  if (!Enabled)
  {
    return true;
  }
  const glm::ivec3 blockPos = WorldPosToBlock(worldPos);
  const glm::ivec3 groundChunk =
      UChunkManager::WorldToChunk(glm::ivec3(blockPos.x, 0, blockPos.z));
  return ShouldKeepChunkLoaded(groundChunk, feetBlockPos, eyePos, cap);
}

void UChunkStreamer::EnsureCollisionChunks(glm::ivec3 feetBlockPos)
{
  if (!Enabled)
  {
    return;
  }

  const glm::ivec3 feetChunk = UChunkManager::WorldToChunk(feetBlockPos);
  LoadPriorityCenter = glm::ivec3(feetChunk.x, 0, feetChunk.z);
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

bool UChunkStreamer::IsCollisionReady(glm::ivec3 feetBlockPos,
                                      int radiusChunks) const
{
  return IsCollisionRingReady(World, feetBlockPos, radiusChunks);
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
    InvalidateTerrainCompleteCache(ground);
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
  const glm::ivec3 feetChunk = UChunkManager::WorldToChunk(feetBlockPos);
  LoadPriorityCenter = glm::ivec3(feetChunk.x, 0, feetChunk.z);
  const glm::ivec3 loadCenter = LoadPriorityCenter;

  std::vector<glm::ivec3> toLoad;
  toLoad.reserve(static_cast<size_t>((2 * RenderDistance + 1) *
                                     (2 * RenderDistance + 1)));
  for (int cx = loadCenter.x - RenderDistance;
       cx <= loadCenter.x + RenderDistance; ++cx)
  {
    for (int cz = loadCenter.z - RenderDistance;
         cz <= loadCenter.z + RenderDistance; ++cz)
    {
      const glm::ivec3 coord(cx, 0, cz);
      if (ProcedurallyGenerated.count(coord) &&
          IsTerrainChunkCompleteCached(coord))
      {
        continue;
      }
      toLoad.push_back(coord);
    }
  }
  std::sort(toLoad.begin(), toLoad.end(),
            [this](const glm::ivec3 &a, const glm::ivec3 &b)
            {
              return ChunkLoadPriorityFor(a) < ChunkLoadPriorityFor(b);
            });

  int loadOps = 0;
  for (const glm::ivec3 &coord : toLoad)
  {
    if (loadOps >= MaxLoadOpsPerFrame)
    {
      break;
    }
    if (!RingPrerequisitesMet(coord))
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

  UnloadDistantChunks(loadCenter, feetBlockPos, eyePos, cap);
}

void UChunkStreamer::PrefetchAhead(glm::ivec3 feet_chunk,
                                   glm::vec3 view_forward_xz,
                                   float movement_speed, float speed_threshold)
{
  if (!Enabled || !AsyncGeneration || !OnRequestAsyncChunk ||
      movement_speed < speed_threshold)
  {
    return;
  }
  view_forward_xz.y = 0.0f;
  if (glm::length(view_forward_xz) < 0.01f)
  {
    return;
  }
  const glm::vec3 forward = glm::normalize(view_forward_xz);
  const glm::ivec3 feet_ground(feet_chunk.x, 0, feet_chunk.z);
  for (int step = 1; step <= 2; ++step)
  {
    const float ahead_blocks =
        static_cast<float>(step * CHUNK_SIZE) + static_cast<float>(CHUNK_SIZE) * 0.5f;
    const glm::vec2 ahead_xz(forward.x * ahead_blocks, forward.z * ahead_blocks);
    const int cx = feet_ground.x +
                   static_cast<int>(std::round(ahead_xz.x / static_cast<float>(CHUNK_SIZE)));
    const int cz = feet_ground.z +
                   static_cast<int>(std::round(ahead_xz.y / static_cast<float>(CHUNK_SIZE)));
    const glm::ivec3 coord(cx, 0, cz);
    if (ProcedurallyGenerated.count(coord) &&
        IsTerrainChunkCompleteCached(coord))
    {
      continue;
    }
    if (OnIsChunkCommitted && OnIsChunkCommitted(coord))
    {
      continue;
    }
    OnRequestAsyncChunk(coord, ChunkLoadPriorityFor(coord) - PriorityParams.ViewAheadBonus);
  }
}

} // namespace cutum
