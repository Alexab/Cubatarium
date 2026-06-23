#ifndef GL_STATE_SCOPE_H
#define GL_STATE_SCOPE_H

#include "Render/Pipeline/GlStateMask.h"

#include "Render/GlIncludes.h"

namespace cutum
{

/// Saves selected OpenGL state on construction and restores it on destruction.
class UGlStateScope
{
public:
  explicit UGlStateScope(GlStateMask mask);
  ~UGlStateScope();

  UGlStateScope(const UGlStateScope &) = delete;
  UGlStateScope &operator=(const UGlStateScope &) = delete;

private:
  GlStateMask Mask{0};

  GLboolean DepthTest{GL_FALSE};
  GLint DepthFunc{0};
  GLboolean DepthMask{GL_TRUE};
  GLboolean Blend{GL_FALSE};
  GLboolean CullFace{GL_FALSE};
  GLboolean StencilTest{GL_FALSE};
  GLint StencilFunc{GL_ALWAYS};
  GLint StencilRef{0};
  GLint StencilValueMask{0xFF};
  GLint StencilWriteMask{0xFF};
  GLint StencilFail{GL_KEEP};
  GLint StencilZFail{GL_KEEP};
  GLint StencilZPass{GL_KEEP};
  GLboolean ColorMask[4]{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};

  GLint Framebuffer{0};
  GLint Viewport[4]{0, 0, 0, 0};
  GLint ActiveTexture{0};
  GLint BoundTexture{0};
  GLint Program{0};
  GLint Vao{0};
};

} // namespace cutum

#endif
