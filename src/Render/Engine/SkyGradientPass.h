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
  void Draw(const std::shared_ptr<UShaderProgram> &sky_shader,
            const glm::vec4 &sky_color, const UUnderwaterFogPass &fog_pass,
            const UWorld::EnvironmentState &env, PerformancePreset preset,
            float elapsed_sec, const glm::mat3 &inv_view_rot,
            const glm::vec3 &camera_pos, float horizon_boost = 0.0f);
};

} // namespace cutum
