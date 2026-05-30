#ifndef FRUSTUM_H
#define FRUSTUM_H

#include "Chunk.h"
#include <array>
#include <glm/glm.hpp>

namespace cutum {

struct Frustum {
 std::array<glm::vec4, 6> planes;

 static Frustum FromViewProjection(const glm::mat4& m)
 {
  Frustum f;
  const glm::mat4 t = glm::transpose(m);
  f.planes[0] = t[3] + t[0];
  f.planes[1] = t[3] - t[0];
  f.planes[2] = t[3] + t[1];
  f.planes[3] = t[3] - t[1];
  f.planes[4] = t[3] + t[2];
  f.planes[5] = t[3] - t[2];
  for (glm::vec4& p : f.planes) {
   const float len = glm::length(glm::vec3(p));
   if (len > 0.0f) {
    p /= len;
   }
  }
  return f;
 }

 bool IntersectsAABB(const glm::vec3& bmin, const glm::vec3& bmax) const
 {
  for (const glm::vec4& plane : planes) {
   const glm::vec3 n(plane);
   glm::vec3 p = bmin;
   if (n.x >= 0.0f) {
    p.x = bmax.x;
   }
   if (n.y >= 0.0f) {
    p.y = bmax.y;
   }
   if (n.z >= 0.0f) {
    p.z = bmax.z;
   }
   if (glm::dot(n, p) + plane.w < 0.0f) {
    return false;
   }
  }
  return true;
 }
};

inline glm::vec3 ChunkAABBMin(glm::ivec3 chunkCoord)
{
 return glm::vec3(
     chunkCoord.x * CHUNK_SIZE,
     chunkCoord.y * CHUNK_SIZE,
     chunkCoord.z * CHUNK_SIZE);
}

inline glm::vec3 ChunkAABBMax(glm::ivec3 chunkCoord)
{
 return ChunkAABBMin(chunkCoord) + glm::vec3(CHUNK_SIZE);
}

}

#endif
