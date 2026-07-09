#include "Render/Engine/SkyGradientPass.h"

#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/UnderwaterFogPass.h"
#include "Render/GlIncludes.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace cutum
{

USkyGradientPass::~USkyGradientPass() { DestroyBuffers(); }

bool USkyGradientPass::EnsureBuffers()
{
  if (SkyVao != 0 && SkyVbo != 0)
  {
    return true;
  }
  static const GLfloat sky_vertices[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
      1.0f,  1.0f,  0.0f, 1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 0.0f, 1.0f};
  glGenVertexArrays(1, &SkyVao);
  glGenBuffers(1, &SkyVbo);
  if (SkyVao == 0 || SkyVbo == 0)
  {
    DestroyBuffers();
    return false;
  }
  glBindVertexArray(SkyVao);
  glBindBuffer(GL_ARRAY_BUFFER, SkyVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(sky_vertices), sky_vertices,
               GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  return true;
}

void USkyGradientPass::DestroyBuffers()
{
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

void USkyGradientPass::Draw(const std::shared_ptr<UShaderProgram> &sky_shader,
                            const glm::vec4 &sky_color,
                            const UUnderwaterFogPass &fog_pass,
                            const UWorld::EnvironmentState &env,
                            PerformancePreset preset, float elapsed_sec,
                            const glm::mat3 &inv_view_rot,
                            const glm::vec3 &camera_pos, float horizon_boost)
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

  glDisable(GL_DEPTH_TEST);

  sky_shader->Use();

  const glm::mat4 sky_matrix = glm::mat4(1.0f);
  sky_shader->SetMat4("mvp_matrix", sky_matrix);
  sky_shader->SetVec4("skyColor", sky_color);
  sky_shader->SetVec3("uFogColor", fog_pass.GetFogColor());
  sky_shader->SetFloat("uTimeOfDay", env.TimeOfDayNormalized);
  sky_shader->SetFloat("uStarVisibility",
                       std::clamp(env.StarVisibility, 0.0f, 1.0f));
  sky_shader->SetFloat("uCloudCoverage",
                       std::clamp(env.CloudCoverage, 0.0f, 1.0f));
  sky_shader->SetFloat("uElapsedSec", std::max(0.0f, elapsed_sec));
  sky_shader->SetMat3("uInvViewRot", inv_view_rot);
  sky_shader->SetVec3("uCameraPos", camera_pos);
  int cloud_steps = 12;
  if (preset == PerformancePreset::Fast)
  {
    cloud_steps = 6;
  }
  else if (preset == PerformancePreset::Quality)
  {
    cloud_steps = 18;
  }
  sky_shader->SetInt("uCloudSteps", cloud_steps);
  sky_shader->SetFloat(
      "uCloudJitter",
      preset == PerformancePreset::Fast
          ? 0.35f
          : (preset == PerformancePreset::Quality ? 0.8f : 0.6f));
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

  glBindVertexArray(SkyVao);
  glBindBuffer(GL_ARRAY_BUFFER, SkyVbo);

  const int vertex_location =
      glGetAttribLocation(sky_shader->GetProgramID(), "aPos");
  if (vertex_location != -1)
  {
    glEnableVertexAttribArray(vertex_location);
    glVertexAttribPointer(vertex_location, 3, GL_FLOAT, GL_FALSE,
                          5 * sizeof(GLfloat), (void *)0);
  }

  const int texcoord_location =
      glGetAttribLocation(sky_shader->GetProgramID(), "aTexCoord");
  if (texcoord_location != -1)
  {
    glEnableVertexAttribArray(texcoord_location);
    glVertexAttribPointer(texcoord_location, 2, GL_FLOAT, GL_FALSE,
                          5 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));
  }

  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  const GLenum error = glGetError();
  if (error != GL_NO_ERROR)
  {
    std::cerr << "OpenGL error after drawing sky: " << error << std::endl;
  }

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  sky_shader->Unuse();

  glEnable(GL_DEPTH_TEST);
}

} // namespace cutum
