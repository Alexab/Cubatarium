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

  // Full six-plane p-vertex test. Frustum::IntersectsAABB skips the
  // near/top/bottom planes (precision guards for huge chunk boxes) and
  // fast-true's on distance — either of which admitted creatures behind the
  // camera. Creature AABBs are small, so the exact test is both cheap and
  // correct here.
  for (const glm::vec4 &plane : frustum.planes)
  {
    const glm::vec3 n(plane);
    glm::vec3 p = bmin;
    if (n.x >= 0.0f)
    {
      p.x = bmax.x;
    }
    if (n.y >= 0.0f)
    {
      p.y = bmax.y;
    }
    if (n.z >= 0.0f)
    {
      p.z = bmax.z;
    }
    if (glm::dot(n, p) + plane.w < 0.0f)
    {
      return false;
    }
  }
  return true;
}

} // namespace cutum

#endif
