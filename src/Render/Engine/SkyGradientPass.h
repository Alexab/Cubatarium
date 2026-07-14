#pragma once

#include "App/Settings/RenderSettings.h"
#include "World/Core/World.h"
#include <glm/glm.hpp>
#include <memory>

namespace cutum
{

class UShaderProgram;
class UUnderwaterFogPass;

class USkyGradientPass
{
public:
  ~USkyGradientPass();
  void Draw(const std::shared_ptr<UShaderProgram> &sky_shader,
            const glm::vec4 &sky_color, const UUnderwaterFogPass &fog_pass,
            const UWorld::EnvironmentState &env, const RenderSettings &render,
            PerformancePreset preset, float elapsed_sec,
            const glm::mat3 &inv_view_rot, const glm::vec3 &camera_pos,
            float horizon_boost = 0.0f);
  void InvalidateGpuResources();
  double GetLastDrawMs() const { return LastDrawMs; }

private:
  bool EnsureBuffers();
  void DestroyBuffers();
  unsigned int SkyVao{0};
  unsigned int SkyVbo{0};
  double LastDrawMs{0.0};
};

} // namespace cutum
