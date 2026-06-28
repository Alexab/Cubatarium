#include "Gui/Preview/CreaturePreviewLayout.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{

CreaturePreviewFit FitCreaturePreview(
    const std::vector<ResolvedCreaturePart> &parts, float targetSpan)
{
  CreaturePreviewFit fit;
  if (parts.empty())
  {
    return fit;
  }

  glm::vec3 minB(1e6f);
  glm::vec3 maxB(-1e6f);
  for (const ResolvedCreaturePart &part : parts)
  {
    const glm::vec3 half = part.sizeBlocks * 0.5f;
    minB = glm::min(minB, part.offsetBlocks - half);
    maxB = glm::max(maxB, part.offsetBlocks + half);
  }

  fit.center = (minB + maxB) * 0.5f;

  float maxRadius = 0.0f;
  for (const ResolvedCreaturePart &part : parts)
  {
    const glm::vec3 half = part.sizeBlocks * 0.5f;
    for (int sx = -1; sx <= 1; sx += 2)
    {
      for (int sy = -1; sy <= 1; sy += 2)
      {
        for (int sz = -1; sz <= 1; sz += 2)
        {
          const glm::vec3 corner =
              part.offsetBlocks +
              glm::vec3(half.x * static_cast<float>(sx),
                        half.y * static_cast<float>(sy),
                        half.z * static_cast<float>(sz));
          maxRadius =
              std::max(maxRadius, glm::length(corner - fit.center));
        }
      }
    }
  }
  maxRadius = std::max(maxRadius, 0.01f);
  constexpr float kRotationMargin = 1.08f;
  fit.uniformScale = targetSpan / (2.0f * maxRadius * kRotationMargin);
  return fit;
}

glm::mat4 CreaturePreviewPartModel(const CreaturePreviewFit &fit,
                                   const ResolvedCreaturePart &part)
{
  const glm::vec3 local =
      fit.uniformScale * (part.offsetBlocks - fit.center);
  const float partScale = std::max(0.01f, fit.uniformScale);
  return glm::translate(glm::mat4(1.0f), local) *
         glm::scale(glm::mat4(1.0f), part.sizeBlocks * partScale);
}

} // namespace cutum
