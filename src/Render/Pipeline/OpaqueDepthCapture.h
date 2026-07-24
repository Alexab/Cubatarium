#ifndef OPAQUE_DEPTH_CAPTURE_H
#define OPAQUE_DEPTH_CAPTURE_H

#include "Render/GlIncludes.h"

#include <memory>

namespace cutum
{

class UShaderProgram;

class UOpaqueDepthCapture
{
public:
  static constexpr GLenum kTextureUnit = GL_TEXTURE3;

  void DestroyGpuResources();
  void CaptureFromDefaultFramebuffer();
  void Bind() const;
  void ApplyShaderUniforms(const std::shared_ptr<UShaderProgram> &shader,
                           bool enable) const;

  bool IsValid() const { return Texture != 0 && Width > 0 && Height > 0; }

private:
  void EnsureSize(int width, int height);

  GLuint Texture{0};
  int Width{0};
  int Height{0};
};

} // namespace cutum

#endif
