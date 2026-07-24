#include "Render/Pipeline/WeatherDepthCapture.h"

#include "Render/Engine/ShaderManager.h"

#include <glm/glm.hpp>

namespace cutum
{

void UWeatherDepthCapture::DestroyGpuResources()
{
  if (Texture != 0)
  {
    glDeleteTextures(1, &Texture);
    Texture = 0;
  }
  Width = 0;
  Height = 0;
}

void UWeatherDepthCapture::EnsureSize(int width, int height)
{
  if (width <= 0 || height <= 0)
  {
    return;
  }
  if (Texture == 0)
  {
    glGenTextures(1, &Texture);
  }
  if (width == Width && height == Height)
  {
    return;
  }
  Width = width;
  Height = height;
  glBindTexture(GL_TEXTURE_2D, Texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, Width, Height, 0,
               GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
#else
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, Width, Height, 0,
               GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
#endif
  glBindTexture(GL_TEXTURE_2D, 0);
}

void UWeatherDepthCapture::CaptureFromDefaultFramebuffer()
{
  GLint viewport[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_VIEWPORT, viewport);
  const int width = viewport[2];
  const int height = viewport[3];
  if (width <= 0 || height <= 0)
  {
    return;
  }
  EnsureSize(width, height);
  glBindTexture(GL_TEXTURE_2D, Texture);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], width,
                      height);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void UWeatherDepthCapture::Bind() const
{
  if (!IsValid())
  {
    return;
  }
  glActiveTexture(kTextureUnit);
  glBindTexture(GL_TEXTURE_2D, Texture);
}

void UWeatherDepthCapture::ApplyShaderUniforms(
    const std::shared_ptr<UShaderProgram> &shader, bool enable) const
{
  if (!shader || !shader->IsValid())
  {
    return;
  }
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  enable = false;
#endif
  shader->SetFloat("uDepthGuard", enable && IsValid() ? 1.0f : 0.0f);
  if (!enable || !IsValid())
  {
    return;
  }
  shader->SetInt("uSceneDepth", static_cast<int>(kTextureUnit - GL_TEXTURE0));
  shader->SetVec2("uDepthScreenSize", glm::vec2(static_cast<float>(Width),
                                                static_cast<float>(Height)));
}

} // namespace cutum
