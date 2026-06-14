#include "render/TransparentPass.h"

namespace cutum
{

std::array<TransparentPassDesc, 4> GetGreedyTransparentPasses()
{
  return {{
      {TransparentPassId::ShellDepth, "ShellDepth", GL_LESS, true, false,
       GL_ALWAYS, 1, true, GreedyShaderMode::ShellDepthPrepass},
      {TransparentPassId::BehindShell, "BehindShell", GL_GREATER, false, true,
       GL_EQUAL, 1, false, GreedyShaderMode::TransparentColor},
      {TransparentPassId::ShellSurface, "ShellSurface", GL_LEQUAL, false, true,
       GL_EQUAL, 1, false, GreedyShaderMode::TransparentColor},
      {TransparentPassId::FuzzyEdges, "FuzzyEdges", GL_LESS, false, true,
       GL_NOTEQUAL, 1, false, GreedyShaderMode::FuzzyOnly},
  }};
}

} // namespace cutum
