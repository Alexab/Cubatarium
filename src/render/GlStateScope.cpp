#include "render/GlStateScope.h"

#include <GL/glew.h>

namespace cutum {

namespace {

bool HasBit(GlStateMask mask, GlStateBit bit)
{
 return (mask & static_cast<GlStateMask>(bit)) != 0;
}

} // namespace

GlStateScope::GlStateScope(GlStateMask mask) : mask_(mask)
{
 if (HasBit(mask_, GlStateBit::ViewportFb)) {
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_);
  glGetIntegerv(GL_VIEWPORT, viewport_);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture_);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture_);
  glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao_);
 }
 if (HasBit(mask_, GlStateBit::DepthTest)) {
  glGetBooleanv(GL_DEPTH_TEST, &depthTest_);
 }
 if (HasBit(mask_, GlStateBit::DepthFunc)) {
  glGetIntegerv(GL_DEPTH_FUNC, &depthFunc_);
 }
 if (HasBit(mask_, GlStateBit::DepthMask)) {
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask_);
 }
 if (HasBit(mask_, GlStateBit::Blend)) {
  glGetBooleanv(GL_BLEND, &blend_);
 }
 if (HasBit(mask_, GlStateBit::CullFace)) {
  glGetBooleanv(GL_CULL_FACE, &cullFace_);
 }
 if (HasBit(mask_, GlStateBit::StencilTest)) {
  glGetBooleanv(GL_STENCIL_TEST, &stencilTest_);
 }
 if (HasBit(mask_, GlStateBit::StencilOps)) {
  glGetIntegerv(GL_STENCIL_FUNC, &stencilFunc_);
  glGetIntegerv(GL_STENCIL_REF, &stencilRef_);
  glGetIntegerv(GL_STENCIL_VALUE_MASK, &stencilValueMask_);
  glGetIntegerv(GL_STENCIL_WRITEMASK, &stencilWriteMask_);
  glGetIntegerv(GL_STENCIL_FAIL, &stencilFail_);
  glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &stencilZFail_);
  glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &stencilZPass_);
 }
 if (HasBit(mask_, GlStateBit::ColorMask)) {
  glGetBooleanv(GL_COLOR_WRITEMASK, colorMask_);
 }
}

GlStateScope::~GlStateScope()
{
 if (HasBit(mask_, GlStateBit::ColorMask)) {
  glColorMask(colorMask_[0], colorMask_[1], colorMask_[2], colorMask_[3]);
 }
 if (HasBit(mask_, GlStateBit::StencilOps)) {
  glStencilFunc(stencilFunc_, stencilRef_, stencilValueMask_);
  glStencilOp(stencilFail_, stencilZFail_, stencilZPass_);
  glStencilMask(stencilWriteMask_);
 }
 if (HasBit(mask_, GlStateBit::StencilTest)) {
  if (stencilTest_) {
   glEnable(GL_STENCIL_TEST);
  } else {
   glDisable(GL_STENCIL_TEST);
  }
 }
 if (HasBit(mask_, GlStateBit::DepthMask)) {
  glDepthMask(depthMask_);
 }
 if (HasBit(mask_, GlStateBit::DepthFunc)) {
  glDepthFunc(static_cast<GLenum>(depthFunc_));
 }
 if (HasBit(mask_, GlStateBit::DepthTest)) {
  if (depthTest_) {
   glEnable(GL_DEPTH_TEST);
  } else {
   glDisable(GL_DEPTH_TEST);
  }
 }
 if (HasBit(mask_, GlStateBit::CullFace)) {
  if (cullFace_) {
   glEnable(GL_CULL_FACE);
  } else {
   glDisable(GL_CULL_FACE);
  }
 }
 if (HasBit(mask_, GlStateBit::Blend)) {
  if (blend_) {
   glEnable(GL_BLEND);
  } else {
   glDisable(GL_BLEND);
  }
 }
 if (HasBit(mask_, GlStateBit::ViewportFb)) {
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer_));
  glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
  glActiveTexture(static_cast<GLenum>(activeTexture_));
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(boundTexture_));
  glUseProgram(static_cast<GLuint>(program_));
  glBindVertexArray(static_cast<GLuint>(vao_));
 }
}

} // namespace cutum
