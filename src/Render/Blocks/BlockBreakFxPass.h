#ifndef BLOCK_BREAK_FX_PASS_H
#define BLOCK_BREAK_FX_PASS_H

#include "Render/Blocks/BlockBreakParticleSystem.h"

#include <glm/glm.hpp>
#include <memory>

typedef unsigned int GLuint;

namespace cutum
{

class UShaderManager;
class UShaderProgram;
class UWorld;

/// Hit chips + break burst debris for the active dig session.
class UBlockBreakFxPass
{
public:
  bool InitShaders(const std::shared_ptr<UShaderManager> &shader_manager);
  void DestroyGpuResources();

  /// Spawns hit/burst debris from break-session transitions, then draws.
  void UpdateAndRender(UWorld &world, float dt_seconds,
                       const glm::mat4 &view_proj, const glm::vec3 &camera_right,
                       const glm::vec3 &camera_up);

private:
  bool EnsureGpu();
  void SyncFromWorld(UWorld &world);
  void DrawInstances(const glm::mat4 &view_proj, const glm::vec3 &camera_right,
                     const glm::vec3 &camera_up);

  UBlockBreakParticleSystem Particles;
  std::shared_ptr<UShaderProgram> Shader;

  GLuint Vao{0};
  GLuint CornerVbo{0};
  GLuint InstanceVbo{0};
  GLuint Ebo{0};
  int IndexCount{0};
  size_t InstanceCapacity{0};

  bool HadSession{false};
  float LastProgress{0.0f};
  float HitProgressCursor{0.0f};
  glm::ivec3 LastBlockPos{0};
};

} // namespace cutum

#endif
