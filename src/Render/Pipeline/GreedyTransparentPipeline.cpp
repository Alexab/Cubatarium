#include "Render/Pipeline/GreedyTransparentPipeline.h"

#include "Render/Backend/RenderBackendCaps.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Pipeline/TransparentPass.h"

#include "Render/GlIncludes.h"
#include <cstdlib>
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

void DrawTransparentSinglePass(IUGreedyTransparentBackend &backend,
                               const GreedyTransparentSettings &settings)
{
  glDisable(GL_STENCIL_TEST);
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_LESS);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  if (settings.logPassNames)
  {
    std::cout << "[Transparent] single-pass color (no stencil)" << std::endl;
  }
  backend.DrawPreparedTransparent(GreedyShaderMode::TransparentColor,
                                  settings.shellAlpha);
}

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

  // Diagnostic A/B: bypass the desktop stencil shell while keeping the same
  // transparent MDI batches, textures, shader, and opaque-depth guard.
  const char *force_single_pass_env =
      std::getenv("CUBA_DEBUG_TRANSPARENT_SINGLE_PASS");
  const bool force_single_pass = force_single_pass_env != nullptr &&
                                 force_single_pass_env[0] != '\0' &&
                                 force_single_pass_env[0] != '0';
  if (GetActiveRenderBackendCaps().PreferSinglePassTransparent ||
      force_single_pass)
  {
    DrawTransparentSinglePass(backend, settings);
    return;
  }

  static int stencil_bits = -1;
  if (stencil_bits < 0)
  {
    glGetIntegerv(GL_STENCIL_BITS, &stencil_bits);
  }

  glEnable(GL_STENCIL_TEST);
  glStencilMask(0xFF);

  for (const TransparentPassDesc &pass : GetGreedyTransparentPasses())
  {
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
