#ifndef FLUIDSURFACEMAP_H
#define FLUIDSURFACEMAP_H

#include "Render/Engine/RenderFogSettings.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
class UChunkMeshCache;

class UFluidSurfaceMap
{
public:
  static constexpr float kNoSurfaceSentinel = URenderFogSettings::NoSurfaceSentinel;
  static constexpr int kMaxFluidShaderSlots = URenderFogSettings::MaxFluidShaderSlots;

  void EnsureGpuResources();
  void DestroyGpuResources();

  bool Update(UBlockWorld &world, UBlockRegistry &registry, UChunkMeshCache &cache,
              glm::ivec3 cameraBlockXZ, int scanHintY, uint64_t meshRevision);

  void Bind(int surfaceYUnit, int fluidIndexUnit) const;

  bool IsValid() const { return Valid; }
  glm::vec2 GetOriginBlockXZ() const { return OriginBlockXZ; }
  glm::vec2 GetInvSizeBlocks() const { return InvSizeBlocks; }

private:
  bool RefreshStaging(UBlockWorld &world, UBlockRegistry &registry,
                      UChunkMeshCache &cache, glm::ivec3 cameraBlockXZ,
                      int scanHintY);

  int SizeBlocks{0};
  glm::ivec2 WindowOriginBlock{0};
  glm::vec2 OriginBlockXZ{0.0f};
  glm::vec2 InvSizeBlocks{0.0f};
  GLuint SurfaceYTex{0};
  GLuint FluidIndexTex{0};
  bool Valid{false};
  int GpuSizeBlocks{0};
  glm::ivec2 LastCameraBlockXZ{INT32_MAX, INT32_MAX};
  uint64_t LastMeshRevision{0};
  std::vector<float> SurfaceStaging;
  std::vector<uint8_t> FluidIndexStaging;
};

} // namespace cutum

#endif
