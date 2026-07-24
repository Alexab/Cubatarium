#include "Render/Weather/WeatherRenderPass.h"

#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Weather/WeatherExposure.h"
#include "World/Core/World.h"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <iostream>

namespace cutum
{

namespace
{

struct ParticleInstanceGpu
{
  glm::vec3 WorldPos;
  float Kind;
  float Size;
  float Pad0;
  float Pad1;
};

constexpr GlStateMask kGlMaskWeatherOverlay =
    kGlMaskOverlay2D | GlStateBit::DepthMask | GlStateBit::CullFace;

} // namespace

bool UWeatherRenderPass::InitShaders(
    const std::shared_ptr<UShaderManager> &shader_manager)
{
  if (!shader_manager)
  {
    return false;
  }

  StreaksShader = shader_manager->CreateShader(
      "weather_streaks", "shaders/vshader_overlay.glsl",
      "shaders/fshader_weather_streaks.glsl");
  if (!StreaksShader || !StreaksShader->IsValid())
  {
    std::cerr << "Failed to create weather streaks shader" << std::endl;
    return false;
  }

  ParticleShader = shader_manager->CreateShader(
      "weather_particle", "shaders/vshader_weather_particle.glsl",
      "shaders/fshader_weather_particle.glsl");
  if (!ParticleShader || !ParticleShader->IsValid())
  {
    std::cerr << "Failed to create weather particle shader" << std::endl;
    return false;
  }

  return true;
}

void UWeatherRenderPass::DestroyGpuResources()
{
  DepthCapture.DestroyGpuResources();
  if (ParticleEbo != 0)
  {
    glDeleteBuffers(1, &ParticleEbo);
    ParticleEbo = 0;
  }
  if (ParticleInstanceVbo != 0)
  {
    glDeleteBuffers(1, &ParticleInstanceVbo);
    ParticleInstanceVbo = 0;
  }
  if (ParticleCornerVbo != 0)
  {
    glDeleteBuffers(1, &ParticleCornerVbo);
    ParticleCornerVbo = 0;
  }
  if (ParticleVao != 0)
  {
    glDeleteVertexArrays(1, &ParticleVao);
    ParticleVao = 0;
  }
  InstanceCapacity = 0;
  ParticleIndexCount = 0;
}

bool UWeatherRenderPass::PrecipActive(const UWorld &world) const
{
  const UWorld::EnvironmentState &env = world.GetEnvironmentState();
  if (env.PrecipitationIntensity <= 0.05f)
  {
    return false;
  }
  const bool raining = env.Weather == UWorld::WeatherType::Rain ||
                       env.TargetWeather == UWorld::WeatherType::Rain ||
                       env.Weather == UWorld::WeatherType::Storm ||
                       env.TargetWeather == UWorld::WeatherType::Storm;
  const bool snowing = env.Weather == UWorld::WeatherType::Snow ||
                       env.TargetWeather == UWorld::WeatherType::Snow;
  return raining || snowing;
}

int UWeatherRenderPass::WeatherKind(const UWorld &world) const
{
  const UWorld::EnvironmentState &env = world.GetEnvironmentState();
  const bool snowing = env.Weather == UWorld::WeatherType::Snow ||
                       env.TargetWeather == UWorld::WeatherType::Snow;
  const bool raining = env.Weather == UWorld::WeatherType::Rain ||
                       env.TargetWeather == UWorld::WeatherType::Rain ||
                       env.Weather == UWorld::WeatherType::Storm ||
                       env.TargetWeather == UWorld::WeatherType::Storm;
  if (snowing && !raining)
  {
    return 2;
  }
  return 1;
}

float UWeatherRenderPass::QualityForPreset(PerformancePreset preset) const
{
  switch (preset)
  {
  case PerformancePreset::Performance:
  case PerformancePreset::Fast:
    return 0.55f;
  case PerformancePreset::Quality:
    return 0.9f;
  case PerformancePreset::Balanced:
  default:
    return 0.75f;
  }
}

int UWeatherRenderPass::ParticleBudget(PerformancePreset preset,
                                       int weather_kind) const
{
  switch (preset)
  {
  case PerformancePreset::Performance:
  case PerformancePreset::Fast:
    return weather_kind == 2 ? 220 : 320;
  case PerformancePreset::Quality:
    return weather_kind == 2 ? 2800 : 3800;
  case PerformancePreset::Balanced:
  default:
    return weather_kind == 2 ? 1200 : 1600;
  }
}

void UWeatherRenderPass::Render(const WeatherRenderContext &ctx,
                                const UWorld &world)
{
  if (ctx.Width <= 0 || ctx.Height <= 0 || ctx.OverlayVao == 0)
  {
    return;
  }

  // Temporarily disabled: sky background streaks look unnatural in current art
  // direction. Keep particles-only weather until a better sky solution lands.
  if (!world.GetLightingSettings().WeatherParticlesEnabled)
  {
    return;
  }

  if (!CanReceiveOutdoorPrecipitation(world, ctx.CameraPos))
  {
    ParticleSystem.Reset();
    return;
  }

  RenderParticlesPass(ctx, world);
}

void UWeatherRenderPass::RenderStreaksPass(const WeatherRenderContext &ctx,
                                           const UWorld &world)
{
  if (!StreaksShader || !StreaksShader->IsValid() || !PrecipActive(world))
  {
    return;
  }

  const auto t_begin = std::chrono::high_resolution_clock::now();
  const UWorld::EnvironmentState &env = world.GetEnvironmentState();
  const uint8_t debug_mode = world.GetLightingSettings().WeatherDebugMode;

  UGlStateScope gl_guard(kGlMaskWeatherOverlay);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  StreaksShader->Use();
  StreaksShader->SetVec2("uResolution", glm::vec2(ctx.Width, ctx.Height));
  StreaksShader->SetFloat("uTimeSec", ctx.ElapsedSec);
  StreaksShader->SetFloat("uIntensity",
                          std::clamp(env.PrecipitationIntensity, 0.0f, 1.0f));
  StreaksShader->SetInt("uWeatherKind", WeatherKind(world));
  StreaksShader->SetFloat("uQuality", QualityForPreset(ctx.Preset));
  const float weather_light = std::clamp(
      std::max(env.DayNightFactor, env.MoonNightFactor * 0.75f), 0.0f, 1.0f);
  StreaksShader->SetFloat("uDayFactor", weather_light);
  StreaksShader->SetFloat("uWind", std::clamp(env.WindStrength, 0.0f, 1.0f));
  StreaksShader->SetFloat("uDebugMode", debug_mode == 1 ? 1.0f : 0.0f);
  DepthCapture.Bind();
  DepthCapture.ApplyShaderUniforms(StreaksShader, debug_mode != 2);
  if (debug_mode == 2)
  {
    StreaksShader->SetFloat("uDepthGuard", 0.0f);
  }

  glBindVertexArray(ctx.OverlayVao);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  StreaksShader->Unuse();

  const auto t_end = std::chrono::high_resolution_clock::now();
  LastStreakMs =
      std::chrono::duration<double, std::milli>(t_end - t_begin).count();
}

bool UWeatherRenderPass::EnsureParticleBuffers()
{
  if (ParticleVao != 0)
  {
    return true;
  }

  const float corners[] = {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};
  const unsigned int indices[] = {0, 1, 2, 0, 2, 3};

  glGenVertexArrays(1, &ParticleVao);
  glGenBuffers(1, &ParticleCornerVbo);
  glGenBuffers(1, &ParticleInstanceVbo);
  glGenBuffers(1, &ParticleEbo);

  glBindVertexArray(ParticleVao);
  glBindBuffer(GL_ARRAY_BUFFER, ParticleCornerVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, ParticleInstanceVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(ParticleInstanceGpu) * 256, nullptr,
               GL_DYNAMIC_DRAW);
  constexpr GLsizei k_stride =
      static_cast<GLsizei>(sizeof(ParticleInstanceGpu));
  glVertexAttribPointer(
      1, 3, GL_FLOAT, GL_FALSE, k_stride,
      reinterpret_cast<void *>(offsetof(ParticleInstanceGpu, WorldPos)));
  glEnableVertexAttribArray(1);
  glVertexAttribDivisor(1, 1);
  glVertexAttribPointer(
      2, 1, GL_FLOAT, GL_FALSE, k_stride,
      reinterpret_cast<void *>(offsetof(ParticleInstanceGpu, Kind)));
  glEnableVertexAttribArray(2);
  glVertexAttribDivisor(2, 1);
  glVertexAttribPointer(
      3, 1, GL_FLOAT, GL_FALSE, k_stride,
      reinterpret_cast<void *>(offsetof(ParticleInstanceGpu, Size)));
  glEnableVertexAttribArray(3);
  glVertexAttribDivisor(3, 1);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ParticleEbo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);

  glBindVertexArray(0);
  ParticleIndexCount = 6;
  InstanceCapacity = 256;
  return ParticleVao != 0;
}

void UWeatherRenderPass::RenderParticlesPass(const WeatherRenderContext &ctx,
                                             const UWorld &world)
{
  if (!ParticleShader || !ParticleShader->IsValid() || !PrecipActive(world))
  {
    return;
  }

  const int weather_kind = WeatherKind(world);
  const int budget = ParticleBudget(ctx.Preset, weather_kind);
  if (budget <= 0)
  {
    return;
  }

  const auto t_begin = std::chrono::high_resolution_clock::now();
  ParticleSystem.Update(world, ctx.CameraPos, ctx.DeltaSec, budget);
  if (ParticleSystem.GetActiveCount() <= 0)
  {
    LastParticleMs = 0.0;
    return;
  }

  if (!EnsureParticleBuffers())
  {
    return;
  }

  std::vector<ParticleInstanceGpu> gpu_instances;
  gpu_instances.reserve(static_cast<size_t>(ParticleSystem.GetActiveCount()));
  for (const WeatherParticleGpuInstance &inst : ParticleSystem.GetInstances())
  {
    ParticleInstanceGpu row;
    row.WorldPos = inst.WorldPos;
    row.Kind = inst.Kind;
    row.Size = inst.Size;
    row.Pad0 = 0.0f;
    row.Pad1 = 0.0f;
    gpu_instances.push_back(row);
  }

  const size_t byte_size = gpu_instances.size() * sizeof(ParticleInstanceGpu);
  if (gpu_instances.size() > InstanceCapacity)
  {
    InstanceCapacity = gpu_instances.size();
    glBindBuffer(GL_ARRAY_BUFFER, ParticleInstanceVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(byte_size), nullptr,
                 GL_DYNAMIC_DRAW);
  }
  glBindBuffer(GL_ARRAY_BUFFER, ParticleInstanceVbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(byte_size),
                  gpu_instances.data());

  const UWorld::EnvironmentState &env = world.GetEnvironmentState();
  const uint8_t debug_mode = world.GetLightingSettings().WeatherDebugMode;

  UGlStateScope gl_guard(kGlMaskWeatherOverlay);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  ParticleShader->Use();
  ParticleShader->SetMat4("uViewProj", ctx.ViewProj);
  ParticleShader->SetVec3("uCameraRight", ctx.CameraRight);
  ParticleShader->SetVec3("uCameraUp", ctx.CameraUp);
  const float weather_light = std::clamp(
      std::max(env.DayNightFactor, env.MoonNightFactor * 0.75f), 0.0f, 1.0f);
  ParticleShader->SetFloat("uDayFactor", weather_light);
  ParticleShader->SetFloat("uIntensity",
                           std::clamp(env.PrecipitationIntensity, 0.0f, 1.0f));

  glBindVertexArray(ParticleVao);
  glDrawElementsInstanced(GL_TRIANGLES, ParticleIndexCount, GL_UNSIGNED_INT,
                          nullptr, static_cast<GLsizei>(gpu_instances.size()));
  glBindVertexArray(0);
  ParticleShader->Unuse();

  const auto t_end = std::chrono::high_resolution_clock::now();
  LastParticleMs =
      std::chrono::duration<double, std::milli>(t_end - t_begin).count();
}

} // namespace cutum
