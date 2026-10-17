#include "Render/Pipeline/GreedyTransparentPipeline.h"

#include "Render/Backend/RenderBackendCaps.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Pipeline/TransparentPass.h"

#include "App/Platform/Log.h"
#include "Render/GlIncludes.h"
#include <cstdlib>
#include <iostream>
#include <string>

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

  static bool stencil_probe_complete = false;
  static GLint stencil_bits = -1;
  static bool framebuffer_has_stencil = false;
  if (!stencil_probe_complete)
  {
    // Drain and report pre-existing GL errors so they cannot be mistaken for
    // errors raised by these diagnostic queries.
    int prior_gl_errors = 0;
    constexpr int kMaxPriorGlErrorsToDrain = 16;
    while (prior_gl_errors < kMaxPriorGlErrorsToDrain &&
           glGetError() != GL_NO_ERROR)
    {
      ++prior_gl_errors;
    }

    GLint draw_framebuffer = -1;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_framebuffer);
    const GLenum framebuffer_binding_error = glGetError();
    const GLenum framebuffer_status =
        glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    const GLenum framebuffer_status_error = glGetError();
    const GLenum stencil_attachment =
        draw_framebuffer == 0 ? GL_STENCIL : GL_STENCIL_ATTACHMENT;
    GLint stencil_attachment_object_type = GL_NONE;
    GLenum stencil_object_type_query_error = GL_NO_ERROR;
    GLenum stencil_query_error = GL_NO_ERROR;
    if (framebuffer_status_error == GL_NO_ERROR &&
        framebuffer_status == GL_FRAMEBUFFER_COMPLETE)
    {
      // GL_STENCIL_BITS is a removed context-framebuffer query in the core
      // profile. Query the bound framebuffer attachment instead; the default
      // framebuffer uses GL_STENCIL while FBOs use GL_STENCIL_ATTACHMENT.
      glGetFramebufferAttachmentParameteriv(
          GL_DRAW_FRAMEBUFFER, stencil_attachment,
          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
          &stencil_attachment_object_type);
      stencil_object_type_query_error = glGetError();
      if (stencil_object_type_query_error == GL_NO_ERROR)
      {
        if (stencil_attachment_object_type == GL_NONE)
        {
          stencil_bits = 0;
        }
        else
        {
          glGetFramebufferAttachmentParameteriv(
              GL_DRAW_FRAMEBUFFER, stencil_attachment,
              GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &stencil_bits);
          stencil_query_error = glGetError();
        }
      }
    }

    const bool query_ok =
        prior_gl_errors < kMaxPriorGlErrorsToDrain &&
        framebuffer_binding_error == GL_NO_ERROR &&
        stencil_object_type_query_error == GL_NO_ERROR &&
        stencil_query_error == GL_NO_ERROR &&
        framebuffer_status_error == GL_NO_ERROR &&
        framebuffer_status == GL_FRAMEBUFFER_COMPLETE && stencil_bits >= 0;
    framebuffer_has_stencil = query_ok && stencil_bits > 0;
    const char *reason = "stencil_available";
    if (framebuffer_binding_error != GL_NO_ERROR ||
        framebuffer_status_error != GL_NO_ERROR ||
        stencil_object_type_query_error != GL_NO_ERROR ||
        stencil_query_error != GL_NO_ERROR ||
        prior_gl_errors >= kMaxPriorGlErrorsToDrain)
    {
      reason = "stencil_query_invalid";
    }
    else if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE)
    {
      reason = "framebuffer_incomplete";
    }
    else if (stencil_bits < 0)
    {
      reason = "stencil_query_invalid";
    }
    else if (stencil_bits == 0)
    {
      reason = "no_stencil_attachment";
    }

    CubatariumLogInfo(
        "Transparent",
        "framebuffer_stencil_bits=" + std::to_string(stencil_bits) +
            " draw_framebuffer=" + std::to_string(draw_framebuffer) +
            " stencil_attachment=" +
            (draw_framebuffer == 0 ? "default" : "fbo") +
            " stencil_attachment_object_type=" +
            std::to_string(static_cast<unsigned int>(
                stencil_attachment_object_type)) +
            " framebuffer_status=" +
            std::to_string(static_cast<unsigned int>(framebuffer_status)) +
            " prior_gl_errors=" + std::to_string(prior_gl_errors) +
            " binding_query_error=" +
            std::to_string(static_cast<unsigned int>(framebuffer_binding_error)) +
            " stencil_query_error=" +
            std::to_string(static_cast<unsigned int>(stencil_query_error)) +
            " stencil_object_type_query_error=" +
            std::to_string(static_cast<unsigned int>(
                stencil_object_type_query_error)) +
            " framebuffer_status_error=" +
            std::to_string(static_cast<unsigned int>(framebuffer_status_error)) +
            " path=" +
            (framebuffer_has_stencil ? "desktop-shell" : "single-pass-fallback") +
            " reason=" + reason);
    stencil_probe_complete = true;
  }

  // The desktop shell algorithm relies on the first pass writing stencil=1
  // and the color passes testing against it. With no stencil attachment,
  // OpenGL treats stencil tests as passing and ignores stencil writes, so the
  // shell mask is unavailable and the multipass blend is not meaningful. An
  // invalid query is also treated conservatively as unknown and uses the
  // single-pass path; only a verified positive bit count enables the shell.
  if (!framebuffer_has_stencil)
  {
    DrawTransparentSinglePass(backend, settings);
    return;
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
