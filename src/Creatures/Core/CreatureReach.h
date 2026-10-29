#ifndef CREATURE_REACH_H
#define CREATURE_REACH_H

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

/// XZ distance between the footprints of two bodies (feet origin + size in
/// blocks); 0 when the footprints touch or overlap. Melee reach is measured
/// edge to edge: a center-to-center distance made every body wider than the
/// attack range (ogre, treeman, mese_monster after the bounds were fitted to
/// the model) unable to ever hit or be hit.
inline float HorizontalBodyGapXZ(const glm::vec3 &originA,
                                 const glm::vec3 &sizeA,
                                 const glm::vec3 &originB,
                                 const glm::vec3 &sizeB)
{
  const float gapX = std::max(0.0f, std::abs(originA.x - originB.x) -
                                        0.5f * (sizeA.x + sizeB.x));
  const float gapZ = std::max(0.0f, std::abs(originA.z - originB.z) -
                                        0.5f * (sizeA.z + sizeB.z));
  return std::sqrt(gapX * gapX + gapZ * gapZ);
}

/// Distance used for melee reach. For two standard 0.6-wide bodies it equals
/// the old center-to-center distance (gap + 0.6), so attack_range values keep
/// their meaning; wide bodies are measured from their edge. Never longer than
/// the center distance, so narrow bodies keep the old reach too.
inline float MeleeDistanceXZ(const glm::vec3 &originA, const glm::vec3 &sizeA,
                             const glm::vec3 &originB, const glm::vec3 &sizeB)
{
  constexpr float kReferencePairWidth = 0.6f;
  const float dx = originA.x - originB.x;
  const float dz = originA.z - originB.z;
  const float center = std::sqrt(dx * dx + dz * dz);
  return std::min(center, HorizontalBodyGapXZ(originA, sizeA, originB, sizeB) +
                              kReferencePairWidth);
}

/// Slab test: distance along a normalized ray to where it enters the box.
/// A ray starting inside the box reports t = 0.
inline bool RayIntersectsAabb(const glm::vec3 &origin, const glm::vec3 &dir,
                              const glm::vec3 &boxMin, const glm::vec3 &boxMax,
                              float maxDistance, float &outT)
{
  float tmin = 0.0f;
  float tmax = maxDistance;
  for (int axis = 0; axis < 3; ++axis)
  {
    if (std::abs(dir[axis]) < 1e-6f)
    {
      if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis])
      {
        return false;
      }
      continue;
    }
    const float inv = 1.0f / dir[axis];
    float t1 = (boxMin[axis] - origin[axis]) * inv;
    float t2 = (boxMax[axis] - origin[axis]) * inv;
    if (t1 > t2)
    {
      std::swap(t1, t2);
    }
    tmin = std::max(tmin, t1);
    tmax = std::min(tmax, t2);
    if (tmin > tmax)
    {
      return false;
    }
  }
  outT = tmin;
  return tmin >= 0.0f && tmin <= maxDistance;
}

} // namespace cutum

#endif
