#include "BlockRaycast.h"
#include "BlockWorld.h"
#include "GridMath.h"
#include <cmath>
#include <limits>

namespace cutum {

namespace {

constexpr float kHalfBlock = 0.5f;

float NextBoundaryT(const glm::vec3& origin, const glm::vec3& direction, int blockCoord, int axis)
{
 if (direction[axis] > 0.0f) {
  return (static_cast<float>(blockCoord) + kHalfBlock - origin[axis]) / direction[axis];
 }
 if (direction[axis] < 0.0f) {
  return (static_cast<float>(blockCoord) - kHalfBlock - origin[axis]) / direction[axis];
 }
 return std::numeric_limits<float>::max();
}

} // namespace

std::optional<BlockRayHit> RaycastSolidBlocks(
    const BlockWorld& world,
    glm::vec3 origin,
    glm::vec3 direction,
    float maxDistance)
{
 const float len = glm::length(direction);
 if (len < 1e-6f) {
  return std::nullopt;
 }
 direction /= len;

 const float eps = 1e-4f;
 glm::ivec3 current = WorldPosToBlock(origin);

 if (!world.IsAir(current)) {
  BlockRayHit hit;
  hit.blockPos = current;
  hit.faceNormal = glm::ivec3(
      direction.x > 0.0f ? -1 : (direction.x < 0.0f ? 1 : 0),
      direction.y > 0.0f ? -1 : (direction.y < 0.0f ? 1 : 0),
      direction.z > 0.0f ? -1 : (direction.z < 0.0f ? 1 : 0));
  hit.distance = 0.0f;
  return hit;
 }

 float t = 0.0f;
 while (t < maxDistance) {
  const glm::vec3 pos = origin + direction * t;

  float tNext = maxDistance;
  int stepAxis = -1;
  const float tx = NextBoundaryT(origin, direction, current.x, 0);
  const float ty = NextBoundaryT(origin, direction, current.y, 1);
  const float tz = NextBoundaryT(origin, direction, current.z, 2);

  if (tx < tNext) {
   tNext = tx;
   stepAxis = 0;
  }
  if (ty < tNext) {
   tNext = ty;
   stepAxis = 1;
  }
  if (tz < tNext) {
   tNext = tz;
   stepAxis = 2;
  }

  if (tNext >= maxDistance) {
   break;
  }

  t = tNext + eps;
  glm::ivec3 next = current;
  if (stepAxis == 0) {
   next.x += (direction.x > 0.0f) ? 1 : -1;
  } else if (stepAxis == 1) {
   next.y += (direction.y > 0.0f) ? 1 : -1;
  } else if (stepAxis == 2) {
   next.z += (direction.z > 0.0f) ? 1 : -1;
  }

  if (!world.IsAir(next)) {
   BlockRayHit hit;
   hit.blockPos = next;
   hit.distance = t;
   hit.faceNormal = current - next;
   return hit;
  }

  current = next;
 }

 return std::nullopt;
}

}
