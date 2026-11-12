#ifndef CREATUREVISIBILITY_H
#define CREATUREVISIBILITY_H

#include "Creatures/Core/CreatureBounds.h"
#include "Render/Camera/Frustum.h"
#include <glm/glm.hpp>
#include <cmath>

namespace cutum
{

inline bool CreatureAabbIntersectsFrustum(const Frustum &frustum,
    const glm::vec3 &bmin,const glm::vec3 &bmax)
{
  for(int axis=0;axis<3;++axis)
    if(!std::isfinite(bmin[axis]) || !std::isfinite(bmax[axis]) || bmin[axis]>bmax[axis])return true;
  for(const auto &plane:frustum.planes)
  {
    const glm::vec3 n(plane);
    const glm::vec3 p(n.x>=0.f?bmax.x:bmin.x,n.y>=0.f?bmax.y:bmin.y,n.z>=0.f?bmax.z:bmin.z);
    if(glm::dot(n,p)+plane.w<0.f)return false;
  }
  return true;
}

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

  return CreatureAabbIntersectsFrustum(frustum,bmin,bmax);
}

} // namespace cutum

#endif
