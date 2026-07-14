#ifndef CREATUREVISIBILITY_H
#define CREATUREVISIBILITY_H

#include "Creatures/Core/CreatureBounds.h"
#include "Render/Camera/Frustum.h"
#include <glm/glm.hpp>

namespace cutum
{

inline bool CreatureBoundsIntersectsFrustum(const Frustum &frustum,
                                            const glm::vec3 &bodyOrigin,
                                            const glm::vec3 &sizeBlocks,
                                            const glm::vec3 &cameraPos,
                                            float maxDistanceBlocks,
                                            bool useFrustumCulling)
{
  const glm::vec3 center = BoundsCollisionCenter(bodyOrigin, sizeBlocks);
  const glm::vec3 half = BoundsHalfExtents(sizeBlocks);
  const glm::vec3 bmin = center - half;
  const glm::vec3 bmax = center + half;

  if (maxDistanceBlocks > 0.f)
  {
    const float dist = glm::length(center - cameraPos);
    if (dist > maxDistanceBlocks)
    {
      return false;
    }
  }

  if (!useFrustumCulling)
  {
    return true;
  }

  return frustum.IntersectsAABB(bmin, bmax, cameraPos, maxDistanceBlocks);
}

} // namespace cutum

#endif
