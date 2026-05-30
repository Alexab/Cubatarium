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
  f.planes[0] = m[3] + m[0];
  f.planes[1] = m[3] - m[0];
  f.planes[2] = m[3] + m[1];
  f.planes[3] = m[3] - m[1];
  f.planes[4] = m[3] + m[2];
  f.planes[5] = m[3] - m[2];
  for (glm::vec4& p : f.planes) {
   const float len = glm::length(glm::vec3(p));
   if (len > 0.0f) {
    p /= len;
   }
  }
  return f;
 }

 /// Conservative chunk culling. Skips near plane (false negatives when camera is inside/near chunk).
 bool IntersectsChunkAABB(const glm::vec3& bmin, const glm::vec3& bmax, const glm::vec3& cameraPos) const
 {
  if (cameraPos.x >= bmin.x && cameraPos.x <= bmax.x
      && cameraPos.y >= bmin.y && cameraPos.y <= bmax.y
      && cameraPos.z >= bmin.z && cameraPos.z <= bmax.z) {
   return true;
  }

  for (size_t i = 0; i < planes.size(); ++i) {
   if (i == 4) {
    continue;
   }
   const glm::vec4& plane = planes[i];
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

inline glm::vec3 ChunkAABBMin(glm::ivec3 chunkCoord, float margin = 2.0f)
{
 return glm::vec3(
     chunkCoord.x * CHUNK_SIZE - margin,
     chunkCoord.y * CHUNK_SIZE - margin,
     chunkCoord.z * CHUNK_SIZE - margin);
}

inline glm::vec3 ChunkAABBMax(glm::ivec3 chunkCoord, float margin = 2.0f)
{
 return glm::vec3(
     chunkCoord.x * CHUNK_SIZE + CHUNK_SIZE + margin,
     chunkCoord.y * CHUNK_SIZE + CHUNK_SIZE + margin,
     chunkCoord.z * CHUNK_SIZE + CHUNK_SIZE + margin);
}

}

#endif
