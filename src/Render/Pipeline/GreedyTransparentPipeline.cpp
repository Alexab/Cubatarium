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

#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)

void DrawGlesTransparentSinglePass(IUGreedyTransparentBackend &backend,
                                   const GreedyTransparentSettings &settings)
{
  // GLES: skip desktop 4-pass stencil shell — drivers/marked texels often yield
  // zero visible fluid (AND-17: no water/lava at all on Android).
  glDisable(GL_STENCIL_TEST);
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_LESS);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  if (settings.logPassNames)
  {
    std::cout << "[Transparent] GLES single-pass color (no stencil)" << std::endl;
  }
  backend.DrawPreparedTransparent(GreedyShaderMode::TransparentColor,
                                  settings.shellAlpha);
}

#endif

} // namespace

void UGreedyTransparentPipeline::Draw(IUGreedyTransparentBackend &backend,
                                      const GreedyTransparentDrawContext &ctx,
                                      const GreedyTransparentSettings &settings)
{
  UGlStateScope glGuard(kGlMaskTransparentPipeline);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);

  backend.PrepareTransparent(ctx);

#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  DrawGlesTransparentSinglePass(backend, settings);
  return;
#endif

  static int stencil_bits = -1;
  if (stencil_bits < 0)
  {
    glGetIntegerv(GL_STENCIL_BITS, &stencil_bits);
  }

  glEnable(GL_STENCIL_TEST);
  glStencilMask(0xFF);

  for (const TransparentPassDesc &pass : GetGreedyTransparentPasses())
  {
    // GPF5: skip fuzzy-only pass on desktop GPU MDI path (edges covered by
    // ShellSurface); saves one MDI submit band per steady frame.
    if (pass.shaderMode == GreedyShaderMode::FuzzyOnly)
    {
      continue;
    }
    if (settings.logPassNames)
    {
      std::cout << "[Transparent] " << pass.debugName << std::endl;
    }
    ApplyPassGlState(pass);
    backend.DrawPreparedTransparent(pass.shaderMode, settings.shellAlpha);
  }
}

} // namespace cutum
