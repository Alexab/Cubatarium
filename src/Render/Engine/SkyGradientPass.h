#pragma once

#include "App/Settings/RenderSettings.h"
#include "World/Core/World.h"
#include <glm/glm.hpp>
#include <future>
#include <memory>
#include <vector>

namespace cutum
{

class UShaderProgram;
class UUnderwaterFogPass;

class USkyGradientPass
{
public:
  ~USkyGradientPass();
  void Draw(const std::shared_ptr<UShaderProgram> &sky_shader,
            const glm::vec4 &sky_color, const UWorld &world,
            const UUnderwaterFogPass &fog_pass,
            const UWorld::EnvironmentState &env, const RenderSettings &render,
            PerformancePreset preset, float elapsed_sec,
            const glm::mat3 &inv_view_rot, const glm::vec3 &camera_pos,
            const glm::mat4 &inv_projection, float horizon_boost = 0.0f);
  void InvalidateGpuResources();
  double GetLastDrawMs() const { return LastDrawMs; }

private:
  bool EnsureBuffers();
  void DestroyBuffers();
  bool UpdateWeatherFieldTexture(const UWorld &world, PerformancePreset preset,
                                 float elapsed_sec,
                                 const glm::vec3 &camera_pos);
  unsigned int SkyVao{0};
  unsigned int SkyVbo{0};
  unsigned int WeatherFieldTexture{0};
  int WeatherFieldResolution{0};
  glm::vec2 WeatherFieldCenter{0.0f};
  float WeatherFieldWorldSize{8192.0f};
  float WeatherFieldLastUpdateSec{-1.0f};
  std::vector<uint8_t> WeatherFieldPixels;
  /// Cloud coverage is ~0.45 s of CPU at 256^2; it is built on a worker and
  /// swapped in when ready instead of stalling the render thread every second.
  struct PendingWeatherField
  {
    std::future<std::vector<uint8_t>> pixels;
    glm::vec2 center{0.0f};
    int resolution{0};
  };
  PendingWeatherField PendingField;
  double LastDrawMs{0.0};
};

} // namespace cutum
