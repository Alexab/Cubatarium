#include "Gui/Preview/CreaturePreviewLayout.h"

#include "Creatures/Visual/Gltf/CreatureGltfModelSpace.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{

CreaturePreviewFit
FitCreaturePreview(const std::vector<ResolvedCreaturePart> &parts,
                   float targetSpan)
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
              part.offsetBlocks + glm::vec3(half.x * static_cast<float>(sx),
                                            half.y * static_cast<float>(sy),
                                            half.z * static_cast<float>(sz));
          maxRadius = std::max(maxRadius, glm::length(corner - fit.center));
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
  const glm::vec3 local = fit.uniformScale * (part.offsetBlocks - fit.center);
  const float partScale = std::max(0.01f, fit.uniformScale);
  return glm::translate(glm::mat4(1.0f), local) *
         glm::scale(glm::mat4(1.0f), part.sizeBlocks * partScale);
}

glm::mat4 GltfPreviewRootMatrix(const CreatureGltfMeshAsset &asset,
                                float targetSpan, float feetOffsetY)
{
  if (asset.primitives.empty())
  {
    return glm::mat4(1.f);
  }

  glm::vec3 minB(1e6f);
  glm::vec3 maxB(-1e6f);
  for (const GltfPrimitiveCpu &prim : asset.primitives)
  {
    const BoneSkeletonCubeMeshCpu &mesh = prim.mesh;
    for (size_t i = 0; i + 2 < mesh.interleavedPosUv.size(); i += 5)
    {
      const glm::vec3 p(mesh.interleavedPosUv[i], mesh.interleavedPosUv[i + 1],
                        mesh.interleavedPosUv[i + 2]);
      minB = glm::min(minB, p);
      maxB = glm::max(maxB, p);
    }
  }

  if (feetOffsetY != 0.f)
  {
    minB.y += feetOffsetY;
    maxB.y += feetOffsetY;
  }

  const glm::vec3 center = (minB + maxB) * 0.5f;
  const glm::vec3 span = maxB - minB;
  // Luanti b3d mobs are elongated on Z; fit preview by body height/width, not tail length.
  const float fitDim =
      std::max({span.y, span.x * 1.25f, span.z * 0.35f, 0.01f});
  constexpr float kRotationMargin = 1.08f;
  const float scale = targetSpan / (fitDim * kRotationMargin);

  glm::mat4 m = glm::scale(glm::mat4(1.f), glm::vec3(scale));
  m = m * GltfEntityConventionMatrix();
  if (feetOffsetY != 0.f)
  {
    m = glm::translate(m, glm::vec3(0.f, feetOffsetY, 0.f));
  }
  m = glm::translate(m, -center);
  return m;
}

} // namespace cutum
