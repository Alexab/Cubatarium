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
                                      glm::ivec3 cameraBlockXZ, int scanHintY)
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
        static_cast<int>(PendingGpuGroundChunks.size());
  };

  if (!sizeChanged && !windowMoved && Valid)
  {
    if (!fluidSurfaceDirty.empty())
    {
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
      int processed = 0;
      for (const glm::ivec3 &groundChunk : dirtyInWindow)
      {
        if (processed >= kMaxChunkUpdatesPerFrame)
        {
          break;
        }
        const FluidSurfaceColumnSlice *slice = cache.GetFluidSurfaceSlice(
            world, registry, groundChunk, scanHintY);
        PatchGroundChunkStaging(SurfaceStaging, FluidIndexStaging,
                                FluidBottomStaging, SizeBlocks,
                                WindowOriginBlock, groundChunk, slice,
                                registry);
        QueueGpuChunk(groundChunk);
        ++processed;
      }
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

  int processed = 0;
  for (int gz = cz - renderDistChunks; gz <= cz + renderDistChunks; ++gz)
  {
    for (int gx = cx - renderDistChunks; gx <= cx + renderDistChunks; ++gx)
    {
      const glm::ivec3 groundChunk(gx, 0, gz);
      const FluidSurfaceColumnSlice *slice =
          cache.GetFluidSurfaceSlice(world, registry, groundChunk, scanHintY);
      PatchGroundChunkStaging(SurfaceStaging, FluidIndexStaging,
                              FluidBottomStaging, SizeBlocks, originBlock,
                              groundChunk, slice, registry);
      ++processed;
    }
  }
  LastFrameStats.DirtyChunksProcessed = processed;
  NeedFullGpuUpload = true;
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
                              int scanHintY, uint64_t meshRevision)
{
  (void)meshRevision;
  if (!RefreshStaging(world, registry, cache, cameraBlockXZ, scanHintY))
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
    int uploaded = 0;
    std::vector<glm::ivec3> uploadedChunks;
    uploadedChunks.reserve(kMaxChunkUpdatesPerFrame);
    for (const glm::ivec3 &groundChunk : PendingGpuGroundChunks)
    {
      if (uploaded >= kMaxChunkUpdatesPerFrame)
      {
        break;
      }
      UploadDirtyChunkGpu(groundChunk);
      uploadedChunks.push_back(groundChunk);
      ++uploaded;
    }
    for (const glm::ivec3 &groundChunk : uploadedChunks)
    {
      PendingGpuGroundChunks.erase(groundChunk);
    }
    Valid = true;
  }

  LastFrameStats.GpuMs = std::chrono::duration<double, std::milli>(
                             std::chrono::high_resolution_clock::now() -
                             gpu_begin)
                             .count();
  LastFrameStats.DirtyChunksPending =
      static_cast<int>(cache.GetFluidSurfaceDirtyGroundChunks().size()) +
      static_cast<int>(PendingGpuGroundChunks.size());
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
