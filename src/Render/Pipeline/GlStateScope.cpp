#include "Render/Pipeline/GlStateScope.h"

#include "Render/GlIncludes.h"

namespace cutum
{

namespace
{

bool HasBit(GlStateMask mask, GlStateBit bit)
{
  return (mask & static_cast<GlStateMask>(bit)) != 0;
}

} // namespace

UGlStateScope::UGlStateScope(GlStateMask mask) : Mask(mask)
{
  if (HasBit(Mask, GlStateBit::ViewportFb))
  {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &Framebuffer);
    glGetIntegerv(GL_VIEWPORT, Viewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &ActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &BoundTexture);
    glGetIntegerv(GL_CURRENT_PROGRAM, &Program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &Vao);
  }
  if (HasBit(Mask, GlStateBit::DepthTest))
  {
    glGetBooleanv(GL_DEPTH_TEST, &DepthTest);
  }
  if (HasBit(Mask, GlStateBit::DepthFunc))
  {
    glGetIntegerv(GL_DEPTH_FUNC, &DepthFunc);
  }
  if (HasBit(Mask, GlStateBit::DepthMask))
  {
    glGetBooleanv(GL_DEPTH_WRITEMASK, &DepthMask);
  }
  if (HasBit(Mask, GlStateBit::Blend))
  {
    glGetBooleanv(GL_BLEND, &Blend);
  }
  if (HasBit(Mask, GlStateBit::CullFace))
  {
    glGetBooleanv(GL_CULL_FACE, &CullFace);
  }
  if (HasBit(Mask, GlStateBit::StencilTest))
  {
    glGetBooleanv(GL_STENCIL_TEST, &StencilTest);
  }
  if (HasBit(Mask, GlStateBit::StencilOps))
  {
    glGetIntegerv(GL_STENCIL_FUNC, &StencilFunc);
    glGetIntegerv(GL_STENCIL_REF, &StencilRef);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, &StencilValueMask);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &StencilWriteMask);
    glGetIntegerv(GL_STENCIL_FAIL, &StencilFail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &StencilZFail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &StencilZPass);
  }
  if (HasBit(Mask, GlStateBit::ColorMask))
  {
    glGetBooleanv(GL_COLOR_WRITEMASK, ColorMask);
  }
}

UGlStateScope::~UGlStateScope()
{
  if (HasBit(Mask, GlStateBit::ColorMask))
  {
    glColorMask(ColorMask[0], ColorMask[1], ColorMask[2], ColorMask[3]);
  }
  if (HasBit(Mask, GlStateBit::StencilOps))
  {
    glStencilFunc(StencilFunc, StencilRef, StencilValueMask);
    glStencilOp(StencilFail, StencilZFail, StencilZPass);
    glStencilMask(StencilWriteMask);
  }
  if (HasBit(Mask, GlStateBit::StencilTest))
  {
    if (StencilTest)
    {
      glEnable(GL_STENCIL_TEST);
    }
    else
    {
      glDisable(GL_STENCIL_TEST);
    }
  }
  if (HasBit(Mask, GlStateBit::DepthMask))
  {
    glDepthMask(DepthMask);
  }
  if (HasBit(Mask, GlStateBit::DepthFunc))
  {
    glDepthFunc(static_cast<GLenum>(DepthFunc));
  }
  if (HasBit(Mask, GlStateBit::DepthTest))
  {
    if (DepthTest)
    {
      glEnable(GL_DEPTH_TEST);
    }
    else
    {
      glDisable(GL_DEPTH_TEST);
    }
  }
  if (HasBit(Mask, GlStateBit::CullFace))
  {
    if (CullFace)
    {
      glEnable(GL_CULL_FACE);
    }
    else
    {
      glDisable(GL_CULL_FACE);
    }
  }
  if (HasBit(Mask, GlStateBit::Blend))
  {
    if (Blend)
    {
      glEnable(GL_BLEND);
    }
    else
    {
      glDisable(GL_BLEND);
    }
  }
  if (HasBit(Mask, GlStateBit::ViewportFb))
  {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(Framebuffer));
    glViewport(Viewport[0], Viewport[1], Viewport[2], Viewport[3]);
    glActiveTexture(static_cast<GLenum>(ActiveTexture));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(BoundTexture));
    glUseProgram(static_cast<GLuint>(Program));
    glBindVertexArray(static_cast<GLuint>(Vao));
  }
}

} // namespace cutum
