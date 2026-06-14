#ifndef TRANSPARENT_PASS_H
#define TRANSPARENT_PASS_H

#include "Render/Pipeline/GreedyShaderMode.h"

#include "Render/GlIncludes.h"
#include <array>

namespace cutum
{

enum class TransparentPassId
{
  ShellDepth,
  BehindShell,
  ShellSurface,
  FuzzyEdges,
};

struct TransparentPassDesc
{
  TransparentPassId id;
  const char *debugName;
  GLenum depthFunc;
  bool depthWrite;
  bool colorWrite;
  GLenum stencilFunc;
  int stencilRef;
  bool stencilReplaceOnPass;
  GreedyShaderMode shaderMode;
};

std::array<TransparentPassDesc, 4> GetGreedyTransparentPasses();

} // namespace cutum

#endif
