#ifndef COLLISIONVOLUME_H
#define COLLISIONVOLUME_H

#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

struct CollisionVolume
{
  glm::vec3 center{0.0f};
  glm::vec3 halfExtents{0.5f};
};

inline bool AabbOverlap(const glm::vec3 &c1, const glm::vec3 &h1,
                        const glm::vec3 &c2, const glm::vec3 &h2)
{
  return std::abs(c1.x - c2.x) < h1.x + h2.x &&
         std::abs(c1.y - c2.y) < h1.y + h2.y &&
         std::abs(c1.z - c2.z) < h1.z + h2.z;
}

} // namespace cutum

#endif
