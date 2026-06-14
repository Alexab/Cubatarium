#include "Render/Pipeline/GreedyTransparentPipeline.h"

#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Pipeline/TransparentPass.h"

#include "Render/GlIncludes.h"
#include <iostream>

namespace cutum
{

namespace
{

void ApplyPassGlState(const TransparentPassDesc &pass)
{
  if (pass.colorWrite)
  {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  }
  else
  {
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  }
  glDepthMask(pass.depthWrite ? GL_TRUE : GL_FALSE);
  glDepthFunc(pass.depthFunc);
  glStencilFunc(pass.stencilFunc, pass.stencilRef, 0xFF);
  if (pass.stencilReplaceOnPass)
  {
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
  }
  else
  {
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  }
}

} // namespace

void UGreedyTransparentPipeline::Draw(IGreedyTransparentBackend &backend,
                                      const GreedyTransparentDrawContext &ctx,
                                      const GreedyTransparentSettings &settings)
{
  UGlStateScope glGuard(kGlMaskTransparentPipeline);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);

  backend.PrepareTransparent(ctx);

  glEnable(GL_STENCIL_TEST);
  glStencilMask(0xFF);

  for (const TransparentPassDesc &pass : GetGreedyTransparentPasses())
  {
    if (settings.logPassNames)
    {
      std::cout << "[transparent] " << pass.debugName << std::endl;
    }
    ApplyPassGlState(pass);
    backend.DrawPreparedTransparent(pass.shaderMode, settings.shellAlpha);
  }
}

} // namespace cutum
