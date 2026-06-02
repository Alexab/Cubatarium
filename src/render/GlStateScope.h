#ifndef GL_STATE_SCOPE_H
#define GL_STATE_SCOPE_H

#include "render/GlStateMask.h"

#include <GL/glew.h>

namespace cutum {

/// Saves selected OpenGL state on construction and restores it on destruction.
class GlStateScope {
public:
 explicit GlStateScope(GlStateMask mask);
 ~GlStateScope();

 GlStateScope(const GlStateScope&) = delete;
 GlStateScope& operator=(const GlStateScope&) = delete;

private:
 GlStateMask mask_{0};

 GLboolean depthTest_{GL_FALSE};
 GLint depthFunc_{0};
 GLboolean depthMask_{GL_TRUE};
 GLboolean blend_{GL_FALSE};
 GLboolean cullFace_{GL_FALSE};
 GLboolean stencilTest_{GL_FALSE};
 GLint stencilFunc_{GL_ALWAYS};
 GLint stencilRef_{0};
 GLint stencilValueMask_{0xFF};
 GLint stencilWriteMask_{0xFF};
 GLint stencilFail_{GL_KEEP};
 GLint stencilZFail_{GL_KEEP};
 GLint stencilZPass_{GL_KEEP};
 GLboolean colorMask_[4]{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};

 GLint framebuffer_{0};
 GLint viewport_[4]{0, 0, 0, 0};
 GLint activeTexture_{0};
 GLint boundTexture_{0};
 GLint program_{0};
 GLint vao_{0};
};

} // namespace cutum

#endif
