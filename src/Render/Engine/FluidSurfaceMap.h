#ifndef FLUIDSURFACEMAP_H
#define FLUIDSURFACEMAP_H

#include "Render/Engine/RenderFogSettings.h"
#include "World/Chunks/ChunkManager.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_set>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
class UChunkMeshCache;

struct FluidSurfaceMapFrameStats
{
  double CpuMs{0.0};
  double GpuMs{0.0};
  int DirtyChunksProcessed{0};
  int DirtyChunksPending{0};
  bool FullRebuild{false};
};

class UFluidSurfaceMap
{
public:
  static constexpr float kNoSurfaceSentinel = URenderFogSettings::NoSurfaceSentinel;
  static constexpr int kMaxFluidShaderSlots = URenderFogSettings::MaxFluidShaderSlots;
  static constexpr int kMaxChunkUpdatesPerFrame = 8;

  void EnsureGpuResources();
  void DestroyGpuResources();

  bool Update(UBlockWorld &world, UBlockRegistry &registry, UChunkMeshCache &cache,
              glm::ivec3 cameraBlockXZ, int scanHintY, uint64_t meshRevision);

  void Bind(int surfaceYUnit, int fluidIndexUnit, int fluidBottomUnit) const;

  bool IsValid() const { return Valid; }
  glm::vec2 GetOriginBlockXZ() const { return OriginBlockXZ; }
  glm::vec2 GetInvSizeBlocks() const { return InvSizeBlocks; }
  const FluidSurfaceMapFrameStats &GetLastFrameStats() const
  {
    return LastFrameStats;
  }
  void ClearLastFrameStats() { LastFrameStats = FluidSurfaceMapFrameStats{}; }

private:
  bool RefreshStaging(UBlockWorld &world, UBlockRegistry &registry,
                      UChunkMeshCache &cache, glm::ivec3 cameraBlockXZ,
                      int scanHintY);
  void UploadFullGpu();
  void UploadDirtyChunkGpu(glm::ivec3 groundChunk);
  void QueueGpuChunk(glm::ivec3 groundChunk);

  int SizeBlocks{0};
  glm::ivec2 WindowOriginBlock{0};
  glm::vec2 OriginBlockXZ{0.0f};
  glm::vec2 InvSizeBlocks{0.0f};
  GLuint SurfaceYTex{0};
  GLuint FluidIndexTex{0};
  GLuint FluidBottomTex{0};
  bool Valid{false};
  int GpuSizeBlocks{0};
  glm::ivec2 LastCameraBlockXZ{INT32_MAX, INT32_MAX};
  uint64_t LastMeshRevision{0};
  bool NeedFullGpuUpload{false};
  std::vector<float> SurfaceStaging;
  std::vector<uint8_t> FluidIndexStaging;
  std::vector<float> FluidBottomStaging;
  std::unordered_set<glm::ivec3, IVec3Hash> PendingGpuGroundChunks;
  FluidSurfaceMapFrameStats LastFrameStats{};
};

} // namespace cutum

#endif
