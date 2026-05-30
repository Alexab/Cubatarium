#ifndef GREEDYMESHMATH_H
#define GREEDYMESHMATH_H

#include "Chunk.h"
#include "ChunkMeshCache.h"
#include "GreedyMesher.h"
#include <glm/gtc/matrix_transform.hpp>

namespace cutum {

inline glm::mat4 RotationForGreedyFace(int axis, int faceSign)
{
 const float halfPi = 1.57079632679f;
 switch (axis) {
 case 0:
  return glm::rotate(glm::mat4(1.0f), faceSign > 0 ? -halfPi : halfPi, glm::vec3(0.0f, 1.0f, 0.0f));
 case 1:
  return glm::rotate(glm::mat4(1.0f), faceSign > 0 ? halfPi : -halfPi, glm::vec3(1.0f, 0.0f, 0.0f));
 case 2:
  if (faceSign > 0) {
   return glm::mat4(1.0f);
  }
  return glm::rotate(glm::mat4(1.0f), 3.14159265359f, glm::vec3(0.0f, 1.0f, 0.0f));
 default:
  return glm::mat4(1.0f);
 }
}

inline FaceInstance MakeFaceInstanceFromQuad(const GreedyQuad& q, glm::ivec3 chunkCoord)
{
 const int uAxis = (q.axis + 1) % 3;
 const int vAxis = (q.axis + 2) % 3;

 glm::vec3 localCenter(0.0f);
 localCenter[q.axis] = static_cast<float>(q.slice) + (q.faceSign > 0 ? 1.0f : 0.0f);
 localCenter[uAxis] = static_cast<float>(q.u) + q.width * 0.5f;
 localCenter[vAxis] = static_cast<float>(q.v) + q.height * 0.5f;

 const glm::vec3 chunkOrigin(
     chunkCoord.x * CHUNK_SIZE,
     chunkCoord.y * CHUNK_SIZE,
     chunkCoord.z * CHUNK_SIZE);
 const glm::vec3 worldCenter = chunkOrigin + localCenter;

 const glm::mat4 R = RotationForGreedyFace(q.axis, q.faceSign);
 glm::mat4 model = glm::translate(glm::mat4(1.0f), worldCenter);
 model = model * R;
 model = glm::scale(model, glm::vec3(
     static_cast<float>(q.width),
     static_cast<float>(q.height),
     1.0f));

 FaceInstance fi;
 fi.id = q.id;
 fi.model = model;
 return fi;
}

}

#endif
