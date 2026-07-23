#include "Render/Engine/FluidSurfaceMap.h"

#include "Blocks/BlockRegistry.h"
#include "Render/Engine/FluidSurfaceMapLogic.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Mesh/FluidSurfaceColumnSlice.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "Render/GlIncludes.h"

namespace cutum
{

namespace
{

void PatchGroundChunkStaging(std::vector<float> &surfaceStaging,
                             std::vector<uint8_t> &fluidIndexStaging,
                             std::vector<float> &fluidBottomStaging,
                             int sizeBlocks, glm::ivec2 originBlock,
                             glm::ivec3 groundChunk,
                             const FluidSurfaceColumnSlice *slice,
                             UBlockRegistry &registry)
{
  PatchFluidSurfaceStagingChunk(
      surfaceStaging, fluidIndexStaging, fluidBottomStaging, sizeBlocks,
      originBlock, groundChunk, slice, registry,
      UFluidSurfaceMap::kNoSurfaceSentinel);
}

bool GroundChunkIntersectsWindow(glm::ivec3 groundChunk, glm::ivec2 originBlock,
                                 int sizeBlocks)
{
  const int chunkMinX = groundChunk.x * CHUNK_SIZE;
  const int chunkMinZ = groundChunk.z * CHUNK_SIZE;
  const int chunkMaxX = chunkMinX + CHUNK_SIZE;
  const int chunkMaxZ = chunkMinZ + CHUNK_SIZE;
  const int windowMaxX = originBlock.x + sizeBlocks;
  const int windowMaxZ = originBlock.y + sizeBlocks;
  return chunkMaxX > originBlock.x && chunkMinX < windowMaxX &&
         chunkMaxZ > originBlock.y && chunkMinZ < windowMaxZ;
}

void ExtractChunkTexels(const std::vector<float> &surfaceStaging,
                        const std::vector<uint8_t> &fluidIndexStaging,
                        const std::vector<float> &fluidBottomStaging,
                        int sizeBlocks, glm::ivec2 originBlock,
                        glm::ivec3 groundChunk, float *outSurface,
                        uint8_t *outIndex, float *outBottom)
{
  for (int localZ = 0; localZ < CHUNK_SIZE; ++localZ)
  {
    for (int localX = 0; localX < CHUNK_SIZE; ++localX)
    {
      const int bx = groundChunk.x * CHUNK_SIZE + localX;
      const int bz = groundChunk.z * CHUNK_SIZE + localZ;
      const int dx = bx - originBlock.x;
      const int dz = bz - originBlock.y;
      const size_t dst = static_cast<size_t>(localZ) * CHUNK_SIZE + localX;
      if (dx < 0 || dz < 0 || dx >= sizeBlocks || dz >= sizeBlocks)
      {
        outSurface[dst] = UFluidSurfaceMap::kNoSurfaceSentinel;
        outIndex[dst] = 0;
        outBottom[dst] = UFluidSurfaceMap::kNoSurfaceSentinel;
        continue;
      }
      const size_t src = static_cast<size_t>(dz) * sizeBlocks + dx;
      outSurface[dst] = surfaceStaging[src];
      outIndex[dst] = fluidIndexStaging[src];
      outBottom[dst] = fluidBottomStaging[src];
    }
  }
}

int ChebyshevChunkDist(glm::ivec3 ground, int cx, int cz)
{
  return std::max(std::abs(ground.x - cx), std::abs(ground.z - cz));
}

void SortGroundChunksNearFirst(std::vector<glm::ivec3> &chunks, int cx, int cz)
{
  std::sort(chunks.begin(), chunks.end(),
            [cx, cz](const glm::ivec3 &a, const glm::ivec3 &b)
            {
              const int da = ChebyshevChunkDist(a, cx, cz);
              const int db = ChebyshevChunkDist(b, cx, cz);
              if (da != db)
              {
                return da < db;
              }
              if (a.z != b.z)
              {
                return a.z < b.z;
              }
              return a.x < b.x;
            });
}

int ChunkUpdateBudget(int pending, double last_wall_ms)
{
  // Manual 201036/192304: burst=32 cold scans → 400–800ms fluid_map_cpu.
  // After a hitch frame, catch up slower so wall does not stack.
  // Also throttle when pending is already above baseline (cold full_rebuild
  // start) — do not wait for wall>40 before leaving the 16→24→32 ramp.
  const bool hitching = last_wall_ms > UFluidSurfaceMap::kWallThrottleMs ||
                        pending > UFluidSurfaceMap::kMaxChunkUpdatesPerFrame;
  const int baseline = hitching ? UFluidSurfaceMap::kMaxChunkUpdatesHitch
                                : UFluidSurfaceMap::kMaxChunkUpdatesPerFrame;
  const int burst = hitching ? UFluidSurfaceMap::kMaxChunkUpdatesHitchBurst
                             : UFluidSurfaceMap::kMaxChunkUpdatesBurst;
  if (pending <= baseline)
  {
    return baseline;
  }
  if (hitching)
  {
    return burst;
  }
  if (pending <= 32)
  {
    return 16;
  }
  if (pending <= 96)
  {
    return 24;
  }
  return burst;
}

int NearFluidDirtyCount(const std::unordered_set<glm::ivec3, IVec3Hash> &dirty,
                        int cx, int cz)
{
  int near = 0;
  for (const glm::ivec3 &ground : dirty)
  {
    if (ChebyshevChunkDist(ground, cx, cz) <= 1)
    {
      ++near;
    }
  }
  return near;
}

int FluidDirtyBudget(int pending, int near_pending, double last_wall_ms)
{
  // Always cover the underfeet water ring when possible; shrink on hitch /
  // cold pending backlog (same gate as ChunkUpdateBudget).
  const bool hitching = last_wall_ms > UFluidSurfaceMap::kWallThrottleMs ||
                        pending > UFluidSurfaceMap::kMaxChunkUpdatesPerFrame;
  const int near_cap = hitching ? 4 : 9;
  return std::max(ChunkUpdateBudget(pending, last_wall_ms),
                  std::min(near_pending, near_cap));
}

} // namespace

void UFluidSurfaceMap::EnsureGpuResources()
{
  if (SurfaceYTex == 0)
  {
    glGenTextures(1, &SurfaceYTex);
  }
  if (FluidIndexTex == 0)
  {
    glGenTextures(1, &FluidIndexTex);
  }
  if (FluidBottomTex == 0)
  {
    glGenTextures(1, &FluidBottomTex);
  }
}

void UFluidSurfaceMap::DestroyGpuResources()
{
  if (SurfaceYTex != 0)
  {
    glDeleteTextures(1, &SurfaceYTex);
    SurfaceYTex = 0;
  }
  if (FluidIndexTex != 0)
  {
    glDeleteTextures(1, &FluidIndexTex);
    FluidIndexTex = 0;
  }
  if (FluidBottomTex != 0)
  {
    glDeleteTextures(1, &FluidBottomTex);
    FluidBottomTex = 0;
  }
  Valid = false;
  SizeBlocks = 0;
  GpuSizeBlocks = 0;
  NeedFullGpuUpload = false;
  PendingGpuGroundChunks.clear();
  PendingRebuildGroundChunks.clear();
  SurfaceStaging.clear();
  FluidIndexStaging.clear();
  FluidBottomStaging.clear();
}

void UFluidSurfaceMap::QueueGpuChunk(glm::ivec3 groundChunk)
{
  PendingGpuGroundChunks.insert(groundChunk);
}

bool UFluidSurfaceMap::RefreshStaging(UBlockWorld &world, UBlockRegistry &registry,
                                      UChunkMeshCache &cache,
                                      glm::ivec3 cameraBlockXZ, int scanHintY,
                                      double lastWallMs)
{
  LastFrameStats = FluidSurfaceMapFrameStats{};
  const auto cpu_begin = std::chrono::high_resolution_clock::now();

  const int renderDistChunks = cache.GetRenderDistanceChunks();
  const int sizeBlocks = (2 * renderDistChunks + 1) * CHUNK_SIZE;
  const int cx = FloorDiv(cameraBlockXZ.x, CHUNK_SIZE);
  const int cz = FloorDiv(cameraBlockXZ.z, CHUNK_SIZE);
  const glm::ivec2 originBlock((cx - renderDistChunks) * CHUNK_SIZE,
                               (cz - renderDistChunks) * CHUNK_SIZE);

  const bool sizeChanged = sizeBlocks != SizeBlocks;
  const bool windowMoved = ShouldRefreshFluidSurfaceWindow(
      WindowOriginBlock.x, WindowOriginBlock.y, originBlock.x, originBlock.y);
  const bool revisionChanged = LastMeshRevision != cache.GetMeshRevision();
  const auto &fluidSurfaceDirty = cache.GetFluidSurfaceDirtyGroundChunks();

  auto finish_cpu = [&]()
  {
    LastFrameStats.CpuMs = std::chrono::duration<double, std::milli>(
                               std::chrono::high_resolution_clock::now() -
                               cpu_begin)
                               .count();
    LastFrameStats.DirtyChunksPending =
        static_cast<int>(fluidSurfaceDirty.size()) +
        static_cast<int>(PendingGpuGroundChunks.size()) +
        static_cast<int>(PendingRebuildGroundChunks.size());
  };

  auto patch_one = [&](glm::ivec3 groundChunk, int hintY)
  {
    const FluidSurfaceColumnSlice *slice =
        cache.GetFluidSurfaceSlice(world, registry, groundChunk, hintY);
    PatchGroundChunkStaging(SurfaceStaging, FluidIndexStaging,
                            FluidBottomStaging, SizeBlocks, WindowOriginBlock,
                            groundChunk, slice, registry);
    QueueGpuChunk(groundChunk);
  };

  auto drain_pending_rebuild = [&](int budget) -> int
  {
    if (PendingRebuildGroundChunks.empty() || budget <= 0)
    {
      return 0;
    }
    SortGroundChunksNearFirst(PendingRebuildGroundChunks, cx, cz);
    const int take = std::min(
        budget, static_cast<int>(PendingRebuildGroundChunks.size()));
    for (int i = 0; i < take; ++i)
    {
      patch_one(PendingRebuildGroundChunks[static_cast<size_t>(i)],
                PendingRebuildScanHintY);
    }
    PendingRebuildGroundChunks.erase(
        PendingRebuildGroundChunks.begin(),
        PendingRebuildGroundChunks.begin() + take);
    return take;
  };

  auto drain_dirty_in_window = [&](int budget) -> int
  {
    if (budget <= 0 || fluidSurfaceDirty.empty())
    {
      return 0;
    }
    std::vector<glm::ivec3> dirtyInWindow;
    dirtyInWindow.reserve(fluidSurfaceDirty.size());
    for (const glm::ivec3 &groundChunk : fluidSurfaceDirty)
    {
      if (GroundChunkIntersectsWindow(groundChunk, WindowOriginBlock,
                                      SizeBlocks))
      {
        dirtyInWindow.push_back(groundChunk);
      }
    }
    SortGroundChunksNearFirst(dirtyInWindow, cx, cz);
    // Underfeet/water ring (r<=1) before far columns so surface does not
    // trail opaque terrain by many frames when dirty backlog is large.
    int processed = 0;
    int left = budget;
    for (const glm::ivec3 &groundChunk : dirtyInWindow)
    {
      if (left <= 0)
      {
        break;
      }
      if (ChebyshevChunkDist(groundChunk, cx, cz) > 1)
      {
        continue;
      }
      patch_one(groundChunk, scanHintY);
      ++processed;
      --left;
    }
    for (const glm::ivec3 &groundChunk : dirtyInWindow)
    {
      if (left <= 0)
      {
        break;
      }
      if (ChebyshevChunkDist(groundChunk, cx, cz) <= 1)
      {
        continue;
      }
      patch_one(groundChunk, scanHintY);
      ++processed;
      --left;
    }
    return processed;
  };

  // Continue a budgeted full/scroll rebuild from a previous frame.
  if (!PendingRebuildGroundChunks.empty() && !sizeChanged && !windowMoved &&
      SizeBlocks == sizeBlocks)
  {
    const int pending = static_cast<int>(PendingRebuildGroundChunks.size()) +
                        static_cast<int>(fluidSurfaceDirty.size());
    const int near_dirty =
        NearFluidDirtyCount(fluidSurfaceDirty, cx, cz);
    const int budget = FluidDirtyBudget(pending, near_dirty, lastWallMs);
    int processed = drain_pending_rebuild(budget);
    // Near dirty must not wait for the entire window rebuild to finish.
    processed += drain_dirty_in_window(budget - processed);
    LastFrameStats.DirtyChunksProcessed = processed;
    LastFrameStats.FullRebuild = !PendingRebuildGroundChunks.empty();
    if (PendingRebuildGroundChunks.empty())
    {
      NeedFullGpuUpload = true;
    }
    LastCameraBlockXZ = glm::ivec2(cameraBlockXZ.x, cameraBlockXZ.z);
    LastMeshRevision = cache.GetMeshRevision();
    finish_cpu();
    return true;
  }
  if (sizeChanged || windowMoved)
  {
    PendingRebuildGroundChunks.clear();
  }

  if (!sizeChanged && !windowMoved && Valid)
  {
    if (!fluidSurfaceDirty.empty())
    {
      const int near_dirty =
          NearFluidDirtyCount(fluidSurfaceDirty, cx, cz);
      const int budget = FluidDirtyBudget(
          static_cast<int>(fluidSurfaceDirty.size()), near_dirty, lastWallMs);
      const int processed = drain_dirty_in_window(budget);
      LastFrameStats.DirtyChunksProcessed = processed;
      LastCameraBlockXZ = glm::ivec2(cameraBlockXZ.x, cameraBlockXZ.z);
      LastMeshRevision = cache.GetMeshRevision();
      finish_cpu();
      return true;
    }
    if (!revisionChanged)
    {
      finish_cpu();
      return true;
    }
    LastMeshRevision = cache.GetMeshRevision();
    finish_cpu();
    return true;
  }

  // Window scroll: reuse overlapping staging and patch only new strips.
  if (!sizeChanged && windowMoved && Valid && SizeBlocks == sizeBlocks)
  {
    const glm::ivec2 oldOrigin = WindowOriginBlock;
    ScrollFluidSurfaceStagingWindow(SurfaceStaging, FluidIndexStaging,
                                    FluidBottomStaging, SizeBlocks, oldOrigin,
                                    originBlock, kNoSurfaceSentinel);
    WindowOriginBlock = originBlock;
    OriginBlockXZ = glm::vec2(static_cast<float>(originBlock.x),
                              static_cast<float>(originBlock.y));
    InvSizeBlocks =
        glm::vec2(1.0f / static_cast<float>(std::max(SizeBlocks, 1)),
                  1.0f / static_cast<float>(std::max(SizeBlocks, 1)));
    PendingGpuGroundChunks.clear();
    PendingRebuildGroundChunks.clear();
    PendingRebuildScanHintY = scanHintY;
    for (int gz = cz - renderDistChunks; gz <= cz + renderDistChunks; ++gz)
    {
      for (int gx = cx - renderDistChunks; gx <= cx + renderDistChunks; ++gx)
      {
        const glm::ivec3 groundChunk(gx, 0, gz);
        if (!FluidSurfaceChunkNeedsStripPatch(groundChunk, oldOrigin, originBlock,
                                              SizeBlocks))
        {
          continue;
        }
        PendingRebuildGroundChunks.push_back(groundChunk);
      }
    }
    const int budget = ChunkUpdateBudget(
        static_cast<int>(PendingRebuildGroundChunks.size()), lastWallMs);
    const int processed = drain_pending_rebuild(budget);
    LastFrameStats.DirtyChunksProcessed = processed;
    LastFrameStats.FullRebuild = !PendingRebuildGroundChunks.empty();
    NeedFullGpuUpload = PendingRebuildGroundChunks.empty();
    LastCameraBlockXZ = glm::ivec2(cameraBlockXZ.x, cameraBlockXZ.z);
    LastMeshRevision = cache.GetMeshRevision();
    finish_cpu();
    return true;
  }

  LastFrameStats.FullRebuild = true;
  SizeBlocks = sizeBlocks;
  WindowOriginBlock = originBlock;
  OriginBlockXZ = glm::vec2(static_cast<float>(originBlock.x),
                            static_cast<float>(originBlock.y));
  InvSizeBlocks =
      glm::vec2(1.0f / static_cast<float>(std::max(SizeBlocks, 1)),
                1.0f / static_cast<float>(std::max(SizeBlocks, 1)));

  const size_t texelCount =
      static_cast<size_t>(SizeBlocks) * static_cast<size_t>(SizeBlocks);
  SurfaceStaging.assign(texelCount, kNoSurfaceSentinel);
  FluidIndexStaging.assign(texelCount, 0);
  FluidBottomStaging.assign(texelCount, kNoSurfaceSentinel);
  PendingGpuGroundChunks.clear();
  PendingRebuildGroundChunks.clear();
  PendingRebuildScanHintY = scanHintY;
  for (int gz = cz - renderDistChunks; gz <= cz + renderDistChunks; ++gz)
  {
    for (int gx = cx - renderDistChunks; gx <= cx + renderDistChunks; ++gx)
    {
      PendingRebuildGroundChunks.emplace_back(gx, 0, gz);
    }
  }
  const int budget = ChunkUpdateBudget(
      static_cast<int>(PendingRebuildGroundChunks.size()), lastWallMs);
  const int processed = drain_pending_rebuild(budget);
  LastFrameStats.DirtyChunksProcessed = processed;
  LastFrameStats.FullRebuild = !PendingRebuildGroundChunks.empty();
  NeedFullGpuUpload = PendingRebuildGroundChunks.empty();
  LastCameraBlockXZ = glm::ivec2(cameraBlockXZ.x, cameraBlockXZ.z);
  LastMeshRevision = cache.GetMeshRevision();
  finish_cpu();
  return true;
}

void UFluidSurfaceMap::UploadFullGpu()
{
  EnsureGpuResources();
  if (SizeBlocks <= 0)
  {
    Valid = false;
    return;
  }

  const bool reallocate = GpuSizeBlocks != SizeBlocks;
  glBindTexture(GL_TEXTURE_2D, SurfaceYTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, SizeBlocks, SizeBlocks, 0, GL_RED,
               GL_FLOAT, SurfaceStaging.data());

  glBindTexture(GL_TEXTURE_2D, FluidIndexTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, SizeBlocks, SizeBlocks, 0, GL_RED,
               GL_UNSIGNED_BYTE, FluidIndexStaging.data());

  glBindTexture(GL_TEXTURE_2D, FluidBottomTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, SizeBlocks, SizeBlocks, 0, GL_RED,
               GL_FLOAT, FluidBottomStaging.data());

  glBindTexture(GL_TEXTURE_2D, 0);
  GpuSizeBlocks = SizeBlocks;
  Valid = true;
  NeedFullGpuUpload = false;
  PendingGpuGroundChunks.clear();
  (void)reallocate;
}

void UFluidSurfaceMap::UploadDirtyChunkGpu(glm::ivec3 groundChunk)
{
  EnsureGpuResources();
  if (!Valid || SizeBlocks <= 0 || GpuSizeBlocks != SizeBlocks)
  {
    NeedFullGpuUpload = true;
    return;
  }

  const int bx0 = groundChunk.x * CHUNK_SIZE;
  const int bz0 = groundChunk.z * CHUNK_SIZE;
  const int dx = bx0 - WindowOriginBlock.x;
  const int dz = bz0 - WindowOriginBlock.y;
  if (dx < 0 || dz < 0 || dx + CHUNK_SIZE > SizeBlocks ||
      dz + CHUNK_SIZE > SizeBlocks)
  {
    return;
  }

  float surfacePatch[CHUNK_SIZE * CHUNK_SIZE];
  uint8_t indexPatch[CHUNK_SIZE * CHUNK_SIZE];
  float bottomPatch[CHUNK_SIZE * CHUNK_SIZE];
  ExtractChunkTexels(SurfaceStaging, FluidIndexStaging, FluidBottomStaging,
                     SizeBlocks, WindowOriginBlock, groundChunk, surfacePatch,
                     indexPatch, bottomPatch);

  glBindTexture(GL_TEXTURE_2D, SurfaceYTex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, dx, dz, CHUNK_SIZE, CHUNK_SIZE, GL_RED,
                  GL_FLOAT, surfacePatch);

  glBindTexture(GL_TEXTURE_2D, FluidIndexTex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, dx, dz, CHUNK_SIZE, CHUNK_SIZE, GL_RED,
                  GL_UNSIGNED_BYTE, indexPatch);

  glBindTexture(GL_TEXTURE_2D, FluidBottomTex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, dx, dz, CHUNK_SIZE, CHUNK_SIZE, GL_RED,
                  GL_FLOAT, bottomPatch);

  glBindTexture(GL_TEXTURE_2D, 0);
}

bool UFluidSurfaceMap::Update(UBlockWorld &world, UBlockRegistry &registry,
                              UChunkMeshCache &cache, glm::ivec3 cameraBlockXZ,
                              int scanHintY, uint64_t meshRevision,
                              double lastWallMs)
{
  (void)meshRevision;
  if (!RefreshStaging(world, registry, cache, cameraBlockXZ, scanHintY,
                      lastWallMs))
  {
    Valid = false;
    return false;
  }

  const auto gpu_begin = std::chrono::high_resolution_clock::now();
  if (NeedFullGpuUpload || GpuSizeBlocks != SizeBlocks || !Valid)
  {
    UploadFullGpu();
  }
  else if (!PendingGpuGroundChunks.empty())
  {
    const int cx = FloorDiv(cameraBlockXZ.x, CHUNK_SIZE);
    const int cz = FloorDiv(cameraBlockXZ.z, CHUNK_SIZE);
    std::vector<glm::ivec3> pending;
    pending.reserve(PendingGpuGroundChunks.size());
    for (const glm::ivec3 &groundChunk : PendingGpuGroundChunks)
    {
      pending.push_back(groundChunk);
    }
    SortGroundChunksNearFirst(pending, cx, cz);
    const int budget =
        ChunkUpdateBudget(static_cast<int>(pending.size()), lastWallMs);
    int uploaded = 0;
    for (const glm::ivec3 &groundChunk : pending)
    {
      if (uploaded >= budget)
      {
        break;
      }
      UploadDirtyChunkGpu(groundChunk);
      PendingGpuGroundChunks.erase(groundChunk);
      ++uploaded;
    }
    Valid = true;
  }

  LastFrameStats.GpuMs = std::chrono::duration<double, std::milli>(
                             std::chrono::high_resolution_clock::now() -
                             gpu_begin)
                             .count();
  LastFrameStats.DirtyChunksPending =
      static_cast<int>(cache.GetFluidSurfaceDirtyGroundChunks().size()) +
      static_cast<int>(PendingGpuGroundChunks.size()) +
      static_cast<int>(PendingRebuildGroundChunks.size());
  return Valid;
}

void UFluidSurfaceMap::Bind(int surfaceYUnit, int fluidIndexUnit,
                            int fluidBottomUnit) const
{
  if (!Valid)
  {
    return;
  }
  glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + surfaceYUnit));
  glBindTexture(GL_TEXTURE_2D, SurfaceYTex);
  glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + fluidIndexUnit));
  glBindTexture(GL_TEXTURE_2D, FluidIndexTex);
  glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + fluidBottomUnit));
  glBindTexture(GL_TEXTURE_2D, FluidBottomTex);
  glActiveTexture(GL_TEXTURE0);
}

} // namespace cutum
