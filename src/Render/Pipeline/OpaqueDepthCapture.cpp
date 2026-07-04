#include "Render/Pipeline/OpaqueDepthCapture.h"

#include "Render/Engine/ShaderManager.h"

#include <glm/glm.hpp>

namespace cutum
{

void UOpaqueDepthCapture::DestroyGpuResources()
{
  if (Texture != 0)
  {
    glDeleteTextures(1, &Texture);
    Texture = 0;
  }
  Width = 0;
  Height = 0;
}

void UOpaqueDepthCapture::EnsureSize(int width, int height)
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
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, Width, Height, 0,
               GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void UOpaqueDepthCapture::CaptureFromDefaultFramebuffer()
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

void UOpaqueDepthCapture::Bind() const
{
  if (!IsValid())
  {
    return;
  }
  glActiveTexture(kTextureUnit);
  glBindTexture(GL_TEXTURE_2D, Texture);
}

void UOpaqueDepthCapture::ApplyShaderUniforms(
    const std::shared_ptr<UShaderProgram> &shader, bool enable) const
{
  if (!shader || !shader->IsValid())
  {
    return;
  }
  shader->SetFloat("uOpaqueDepthGuard", enable && IsValid() ? 1.0f : 0.0f);
  if (!enable || !IsValid())
  {
    return;
  }
  shader->SetInt("uOpaqueDepthMap", static_cast<int>(kTextureUnit - GL_TEXTURE0));
  shader->SetVec2("uOpaqueDepthScreenSize",
                  glm::vec2(static_cast<float>(Width),
                            static_cast<float>(Height)));
  shader->SetFloat("uOpaqueDepthBias", 0.00002f);
}

} // namespace cutum
