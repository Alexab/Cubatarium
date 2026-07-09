#pragma once

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
            float horizon_boost = 0.0f);
};

} // namespace cutum
