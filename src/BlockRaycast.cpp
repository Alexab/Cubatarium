#include "BlockRaycast.h"
#include "BlockWorld.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cutum {

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

 glm::ivec3 voxel = glm::ivec3(
     static_cast<int>(std::floor(origin.x)),
     static_cast<int>(std::floor(origin.y)),
     static_cast<int>(std::floor(origin.z)));

 const glm::ivec3 step(
     direction.x > 0.0f ? 1 : (direction.x < 0.0f ? -1 : 0),
     direction.y > 0.0f ? 1 : (direction.y < 0.0f ? -1 : 0),
     direction.z > 0.0f ? 1 : (direction.z < 0.0f ? -1 : 0));

 glm::vec3 tDelta(
     step.x != 0 ? std::abs(1.0f / direction.x) : std::numeric_limits<float>::max(),
     step.y != 0 ? std::abs(1.0f / direction.y) : std::numeric_limits<float>::max(),
     step.z != 0 ? std::abs(1.0f / direction.z) : std::numeric_limits<float>::max());

 glm::vec3 tMax;
 if (step.x > 0) {
  tMax.x = (static_cast<float>(voxel.x + 1) - origin.x) * tDelta.x;
 } else if (step.x < 0) {
  tMax.x = (static_cast<float>(voxel.x) - origin.x) * tDelta.x;
 } else {
  tMax.x = std::numeric_limits<float>::max();
 }
 if (step.y > 0) {
  tMax.y = (static_cast<float>(voxel.y + 1) - origin.y) * tDelta.y;
 } else if (step.y < 0) {
  tMax.y = (static_cast<float>(voxel.y) - origin.y) * tDelta.y;
 } else {
  tMax.y = std::numeric_limits<float>::max();
 }
 if (step.z > 0) {
  tMax.z = (static_cast<float>(voxel.z + 1) - origin.z) * tDelta.z;
 } else if (step.z < 0) {
  tMax.z = (static_cast<float>(voxel.z) - origin.z) * tDelta.z;
 } else {
  tMax.z = std::numeric_limits<float>::max();
 }

 glm::ivec3 lastNormal(0);
 float traveled = 0.0f;

 for (int i = 0; i < 512 && traveled <= maxDistance; ++i) {
  const glm::ivec3 checkPos = voxel;
  if (!world.IsAir(checkPos)) {
   BlockRayHit hit;
   hit.blockPos = checkPos;
   hit.faceNormal = lastNormal;
   if (hit.faceNormal == glm::ivec3(0)) {
    hit.faceNormal = glm::ivec3(-step.x, -step.y, -step.z);
   }
   hit.distance = traveled;
   return hit;
  }

  if (tMax.x < tMax.y) {
   if (tMax.x < tMax.z) {
    lastNormal = glm::ivec3(-step.x, 0, 0);
    voxel.x += step.x;
    traveled = tMax.x;
    tMax.x += tDelta.x;
   } else {
    lastNormal = glm::ivec3(0, 0, -step.z);
    voxel.z += step.z;
    traveled = tMax.z;
    tMax.z += tDelta.z;
   }
  } else {
   if (tMax.y < tMax.z) {
    lastNormal = glm::ivec3(0, -step.y, 0);
    voxel.y += step.y;
    traveled = tMax.y;
    tMax.y += tDelta.y;
   } else {
    lastNormal = glm::ivec3(0, 0, -step.z);
    voxel.z += step.z;
    traveled = tMax.z;
    tMax.z += tDelta.z;
   }
  }
 }

 return std::nullopt;
}

}
