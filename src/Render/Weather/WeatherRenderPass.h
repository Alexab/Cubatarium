#ifndef WEATHER_RENDER_PASS_H
#define WEATHER_RENDER_PASS_H

#include "App/Settings/RenderSettings.h"
#include "Render/Pipeline/WeatherDepthCapture.h"
#include "Render/Weather/WeatherParticleSystem.h"

#include <glm/glm.hpp>
#include <memory>

typedef unsigned int GLuint;

namespace cutum
{

class UShaderManager;
class UShaderProgram;
class UWorld;

struct WeatherRenderContext
{
  int Width{0};
  int Height{0};
  GLuint OverlayVao{0};
  glm::mat4 ViewProj{1.0f};
  glm::vec3 CameraPos{0.0f};
  glm::vec3 CameraRight{1.0f, 0.0f, 0.0f};
  glm::vec3 CameraUp{0.0f, 1.0f, 0.0f};
  float ElapsedSec{0.0f};
  float DeltaSec{0.0f};
  PerformancePreset Preset{PerformancePreset::Balanced};
};

class UWeatherRenderPass
{
public:
  bool InitShaders(const std::shared_ptr<UShaderManager> &shader_manager);
  void DestroyGpuResources();

  void Render(const WeatherRenderContext &ctx, const UWorld &world);

  double GetLastStreakMs() const { return LastStreakMs; }
  double GetLastParticleMs() const { return LastParticleMs; }
  int GetActiveParticleCount() const { return ParticleSystem.GetActiveCount(); }

private:
  bool PrecipActive(const UWorld &world) const;
  int WeatherKind(const UWorld &world) const;
  float QualityForPreset(PerformancePreset preset) const;
  int ParticleBudget(PerformancePreset preset, int weather_kind) const;

  void RenderStreaksPass(const WeatherRenderContext &ctx, const UWorld &world);
  void RenderParticlesPass(const WeatherRenderContext &ctx,
                           const UWorld &world);
  bool EnsureParticleBuffers();

  std::shared_ptr<UShaderProgram> StreaksShader;
  std::shared_ptr<UShaderProgram> ParticleShader;
  UWeatherDepthCapture DepthCapture;
  UWeatherParticleSystem ParticleSystem;

  GLuint ParticleVao{0};
  GLuint ParticleCornerVbo{0};
  GLuint ParticleInstanceVbo{0};
  GLuint ParticleEbo{0};
  GLsizei ParticleIndexCount{0};
  size_t InstanceCapacity{0};

  double LastStreakMs{0.0};
  double LastParticleMs{0.0};
  float ParticleCooldownSec{0.0f};
};

} // namespace cutum

#endif
