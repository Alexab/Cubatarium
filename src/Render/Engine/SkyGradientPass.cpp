#include "Render/Engine/SkyGradientPass.h"

#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/UnderwaterFogPass.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/RenderTextureUnits.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>

namespace cutum
{

namespace
{
constexpr int kAttrPos = 0;
constexpr int kAttrTexCoord = 1;
constexpr GLfloat kSkyVertices[] = {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, -1.0f,
                                    0.0f,  1.0f,  0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                                    1.0f,  -1.0f, 1.0f, 0.0f, 0.0f, 1.0f};
} // namespace

USkyGradientPass::~USkyGradientPass() { DestroyBuffers(); }

void USkyGradientPass::InvalidateGpuResources() { DestroyBuffers(); }

bool USkyGradientPass::EnsureBuffers()
{
  if (SkyVao != 0 && SkyVbo != 0)
  {
    return true;
  }
  glGenVertexArrays(1, &SkyVao);
  glGenBuffers(1, &SkyVbo);
  if (SkyVao == 0 || SkyVbo == 0)
  {
    DestroyBuffers();
    return false;
  }
  glBindVertexArray(SkyVao);
  glBindBuffer(GL_ARRAY_BUFFER, SkyVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kSkyVertices), kSkyVertices,
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(kAttrPos);
  glVertexAttribPointer(kAttrPos, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                        (void *)0);
  glEnableVertexAttribArray(kAttrTexCoord);
  glVertexAttribPointer(kAttrTexCoord, 2, GL_FLOAT, GL_FALSE,
                        5 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  return true;
}

void USkyGradientPass::DestroyBuffers()
{
  if (WeatherFieldTexture != 0)
  {
    glDeleteTextures(1, &WeatherFieldTexture);
    WeatherFieldTexture = 0;
  }
  WeatherFieldResolution = 0;
  WeatherFieldPixels.clear();
  if (SkyVbo != 0)
  {
    glDeleteBuffers(1, &SkyVbo);
    SkyVbo = 0;
  }
  if (SkyVao != 0)
  {
    glDeleteVertexArrays(1, &SkyVao);
    SkyVao = 0;
  }
}

bool USkyGradientPass::UpdateWeatherFieldTexture(const UWorld &world,
                                                 PerformancePreset preset,
                                                 float elapsed_sec,
                                                 const glm::vec3 &camera_pos)
{
  const int resolution = preset == PerformancePreset::Fast ||
                                 preset == PerformancePreset::Performance
                             ? 128
                             : 256;
  constexpr float kWorldSize = 8192.0f;
  constexpr float kCenterQuantum = 512.0f;
  const glm::vec2 center(
      std::floor(camera_pos.x / kCenterQuantum) * kCenterQuantum +
          kCenterQuantum * 0.5f,
      std::floor(camera_pos.z / kCenterQuantum) * kCenterQuantum +
          kCenterQuantum * 0.5f);
  const bool resized = resolution != WeatherFieldResolution;
  const bool moved = center != WeatherFieldCenter;
  const bool stale = WeatherFieldLastUpdateSec < 0.0f ||
                     elapsed_sec - WeatherFieldLastUpdateSec >= 1.0f;
  const bool changed = resized || moved || stale;
  if (changed)
  {
    WeatherFieldWorldSize = kWorldSize;
    WeatherFieldResolution = resolution;
    WeatherFieldCenter = center;
    WeatherFieldLastUpdateSec = elapsed_sec;
    world.BuildWeatherCoverageField(center, kWorldSize, resolution,
                                    WeatherFieldPixels);
  }
  if (WeatherFieldPixels.empty())
  {
    return false;
  }
  if (WeatherFieldTexture == 0)
  {
    glGenTextures(1, &WeatherFieldTexture);
  }
  if (WeatherFieldTexture == 0)
  {
    return false;
  }
  if (changed)
  {
    glActiveTexture(RenderTextureUnitEnum(RenderTextureUnit::WeatherField));
    glBindTexture(GL_TEXTURE_2D, WeatherFieldTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, resolution, resolution, 0, GL_RED,
                 GL_UNSIGNED_BYTE, WeatherFieldPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
  }
  return true;
}

void USkyGradientPass::Draw(
    const std::shared_ptr<UShaderProgram> &sky_shader,
    const glm::vec4 &sky_color, const UWorld &world,
    const UUnderwaterFogPass &fog_pass, const UWorld::EnvironmentState &env,
    const RenderSettings &render, PerformancePreset preset, float elapsed_sec,
    const glm::mat3 &inv_view_rot, const glm::vec3 &camera_pos,
    const glm::mat4 &inv_projection, float horizon_boost)
{
  if (!sky_shader || !sky_shader->IsValid())
  {
    std::cerr << "Sky shader is not linked!" << std::endl;
    return;
  }
  if (!EnsureBuffers())
  {
    std::cerr << "Sky pass buffers are not initialized!" << std::endl;
    return;
  }

  const auto t_begin = std::chrono::high_resolution_clock::now();
  const bool weather_field_valid =
      UpdateWeatherFieldTexture(world, preset, elapsed_sec, camera_pos);

  glDisable(GL_DEPTH_TEST);

  sky_shader->Use();

  const glm::mat4 sky_matrix = glm::mat4(1.0f);
  sky_shader->SetMat4("mvp_matrix", sky_matrix);
  sky_shader->SetVec4("skyColor", sky_color);
  sky_shader->SetVec3("uFogColor", fog_pass.GetFogColor());
  sky_shader->SetFloat("uUnderwaterSkyAmount",
                       fog_pass.GetUnderwaterSkyAmount());
  sky_shader->SetFloat("uScreenWaterlineNdc", fog_pass.GetScreenWaterlineNdc());
  sky_shader->SetVec3("uUnderwaterFogColor", fog_pass.GetUnderwaterFogColor());
  sky_shader->SetFloat("uTimeOfDay", env.TimeOfDayNormalized);
  sky_shader->SetFloat("uStarVisibility",
                       std::clamp(env.StarVisibility, 0.0f, 1.0f));
  sky_shader->SetFloat("uCloudCoverage",
                       std::clamp(env.CloudCoverage, 0.0f, 1.0f));
  sky_shader->SetFloat("uElapsedSec", std::max(0.0f, elapsed_sec));
  sky_shader->SetVec2("uWindDirection", env.WindDirection);
  sky_shader->SetFloat("uWindStrength",
                       std::clamp(env.WindStrength, 0.0f, 1.0f));
  sky_shader->SetMat3("uInvViewRot", inv_view_rot);
  sky_shader->SetMat4("uInvProjection", inv_projection);
  sky_shader->SetFloat("uPerspective",
                       std::abs(inv_projection[3][3]) < 1.0e-6f ? 1.0f : 0.0f);
  sky_shader->SetInt(
      "uWeatherField",
      RenderTextureUnitIndex(RenderTextureUnit::WeatherField));
  sky_shader->SetFloat("uWeatherFieldValid", weather_field_valid ? 1.0f : 0.0f);
  sky_shader->SetVec2("uWeatherFieldCenter", WeatherFieldCenter);
  sky_shader->SetFloat("uWeatherFieldWorldSize", WeatherFieldWorldSize);
  sky_shader->SetMat3("uStarCelestialInv", env.StarCelestialInv);
  sky_shader->SetVec3("uCameraPos", camera_pos);
  constexpr int kMaxBodies = 4;
  sky_shader->SetInt("uCelestialCount",
                     static_cast<int>(std::min<size_t>(
                         env.CelestialBodies.size(), kMaxBodies)));
  for (int i = 0; i < kMaxBodies; ++i)
  {
    const std::string idx = std::to_string(i);
    if (i < static_cast<int>(env.CelestialBodies.size()))
    {
      const UWorld::UCelestialBodyVisual &body = env.CelestialBodies[i];
      sky_shader->SetVec3("uCelestialDir[" + idx + "]", body.DirectionWorld);
      sky_shader->SetVec3("uCelestialColor[" + idx + "]", body.Color);
      sky_shader->SetFloat("uCelestialIntensity[" + idx + "]", body.Intensity);
      sky_shader->SetFloat("uCelestialAngularSizeDeg[" + idx + "]",
                           body.AngularSizeDeg);
      sky_shader->SetInt("uCelestialType[" + idx + "]",
                         body.Type == UWorld::CelestialBodyType::Moon ? 1 : 0);
    }
    else
    {
      sky_shader->SetVec3("uCelestialDir[" + idx + "]",
                          glm::vec3(0.0f, 1.0f, 0.0f));
      sky_shader->SetVec3("uCelestialColor[" + idx + "]", glm::vec3(0.0f));
      sky_shader->SetFloat("uCelestialIntensity[" + idx + "]", 0.0f);
      sky_shader->SetFloat("uCelestialAngularSizeDeg[" + idx + "]", 0.0f);
      sky_shader->SetInt("uCelestialType[" + idx + "]", 0);
    }
  }
  sky_shader->SetFloat(
      "uFogHorizonBlend",
      std::clamp(fog_pass.GetFogHorizonBlend() + horizon_boost, 0.0f, 1.0f));
  sky_shader->SetFloat("uHorizonFogRadial",
                       render.HorizonFogRadial ? 1.0f : 0.0f);
  sky_shader->SetFloat("uHorizonFogCelestialTint",
                       render.HorizonFogCelestialTint ? 1.0f : 0.0f);
  sky_shader->SetFloat("uFogHorizonElevation",
                       fog_pass.GetFogHorizonElevation());

  glBindVertexArray(SkyVao);
  if (weather_field_valid)
  {
    glActiveTexture(RenderTextureUnitEnum(RenderTextureUnit::WeatherField));
    glBindTexture(GL_TEXTURE_2D, WeatherFieldTexture);
  }
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
  if (weather_field_valid)
  {
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
  }
  glBindVertexArray(0);
  sky_shader->Unuse();

  glEnable(GL_DEPTH_TEST);

  const auto t_end = std::chrono::high_resolution_clock::now();
  LastDrawMs =
      std::chrono::duration<double, std::milli>(t_end - t_begin).count();
}

} // namespace cutum
