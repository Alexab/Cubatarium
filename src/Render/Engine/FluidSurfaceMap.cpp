#include "Render/Engine/FluidSurfaceMap.h"

#include "Blocks/BlockRegistry.h"
#include "Render/Engine/FluidSurfaceMapLogic.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Mesh/FluidSurfaceColumnSlice.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Render/GlIncludes.h"

namespace cutum
{

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
  Valid = false;
  SizeBlocks = 0;
  GpuSizeBlocks = 0;
  SurfaceStaging.clear();
  FluidIndexStaging.clear();
}

bool UFluidSurfaceMap::RefreshStaging(UBlockWorld &world, UBlockRegistry &registry,
                                      UChunkMeshCache &cache,
                                      glm::ivec3 cameraBlockXZ, int scanHintY)
{
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
  if (!sizeChanged && !windowMoved && !revisionChanged && Valid)
  {
    return true;
  }

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

  for (int dz = 0; dz < SizeBlocks; ++dz)
  {
    for (int dx = 0; dx < SizeBlocks; ++dx)
    {
      const int bx = originBlock.x + dx;
      const int bz = originBlock.y + dz;
      const glm::ivec3 groundChunk(FloorDiv(bx, CHUNK_SIZE), 0,
                                   FloorDiv(bz, CHUNK_SIZE));
      const int localX = bx - groundChunk.x * CHUNK_SIZE;
      const int localZ = bz - groundChunk.z * CHUNK_SIZE;

      const FluidSurfaceColumnSlice *slice =
          cache.GetFluidSurfaceSlice(world, registry, groundChunk, scanHintY);
      const size_t idx = static_cast<size_t>(dz) * SizeBlocks + dx;
      if (!slice || !slice->HasSurface(localX, localZ))
      {
        continue;
      }
      SurfaceStaging[idx] =
          BlockTopY(static_cast<int>(slice->surfaceBlockY[localZ][localX]));
      FluidIndexStaging[idx] =
          FluidSurfaceIndexForBlock(slice->fluidId[localZ][localX], registry);
    }
  }

  LastCameraBlockXZ = glm::ivec2(cameraBlockXZ.x, cameraBlockXZ.z);
  LastMeshRevision = cache.GetMeshRevision();
  return true;
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

  EnsureGpuResources();
  if (SizeBlocks <= 0)
  {
    Valid = false;
    return false;
  }

  const bool reallocate = GpuSizeBlocks != SizeBlocks;
  glBindTexture(GL_TEXTURE_2D, SurfaceYTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (reallocate)
  {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, SizeBlocks, SizeBlocks, 0, GL_RED,
                 GL_FLOAT, SurfaceStaging.data());
  }
  else
  {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SizeBlocks, SizeBlocks, GL_RED,
                    GL_FLOAT, SurfaceStaging.data());
  }

  glBindTexture(GL_TEXTURE_2D, FluidIndexTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (reallocate)
  {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, SizeBlocks, SizeBlocks, 0, GL_RED,
                 GL_UNSIGNED_BYTE, FluidIndexStaging.data());
    GpuSizeBlocks = SizeBlocks;
  }
  else
  {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SizeBlocks, SizeBlocks, GL_RED,
                    GL_UNSIGNED_BYTE, FluidIndexStaging.data());
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  Valid = true;
  return true;
}

void UFluidSurfaceMap::Bind(int surfaceYUnit, int fluidIndexUnit) const
{
  if (!Valid)
  {
    return;
  }
  glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + surfaceYUnit));
  glBindTexture(GL_TEXTURE_2D, SurfaceYTex);
  glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + fluidIndexUnit));
  glBindTexture(GL_TEXTURE_2D, FluidIndexTex);
  glActiveTexture(GL_TEXTURE0);
}

} // namespace cutum
