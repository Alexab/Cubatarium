#ifndef GREEDYMESHMATH_H
#define GREEDYMESHMATH_H

#include "Chunk.h"
#include "ChunkMeshCache.h"
#include "GreedyMesher.h"
#include <glm/gtc/matrix_transform.hpp>

namespace cutum {

namespace {

constexpr float kFaceEpsilon = 0.002f;

inline int FaceIndexFromGreedy(int axis, int faceSign)
{
 if (axis == 2) {
  return faceSign > 0 ? 0 : 2;
 }
 if (axis == 0) {
  return faceSign > 0 ? 1 : 3;
 }
 return faceSign > 0 ? 4 : 5;
}

} // namespace

inline FaceInstance MakeFaceInstanceFromQuad(const GreedyQuad& q, glm::ivec3 chunkCoord)
{
 const int uAxis = (q.axis + 1) % 3;
 const int vAxis = (q.axis + 2) % 3;

 glm::vec3 uDir(0.0f);
 glm::vec3 vDir(0.0f);
 glm::vec3 nDir(0.0f);
 uDir[uAxis] = 1.0f;
 vDir[vAxis] = 1.0f;
 nDir[q.axis] = static_cast<float>(q.faceSign);

 const int quadWidth = q.width;
 const int quadHeight = q.height;

 // Keep CCW winding for face culling; do not swap merged width/height.
 if (glm::dot(glm::cross(uDir, vDir), nDir) < 0.0f) {
  vDir = -vDir;
 }

 const glm::vec3 chunkOrigin(
     static_cast<float>(chunkCoord.x * CHUNK_SIZE),
     static_cast<float>(chunkCoord.y * CHUNK_SIZE),
     static_cast<float>(chunkCoord.z * CHUNK_SIZE));

 // Min corner of the merged face (unit quad uses local UV 0..1).
 glm::vec3 corner = chunkOrigin;
 corner[q.axis] = static_cast<float>(q.slice)
     + (q.faceSign > 0 ? 0.5f : -0.5f);
 corner[uAxis] = static_cast<float>(q.u) - 0.5f;
 corner[vAxis] = static_cast<float>(q.v) - 0.5f;
 corner += nDir * kFaceEpsilon;

 glm::mat4 basis(1.0f);
 basis[0] = glm::vec4(uDir, 0.0f);
 basis[1] = glm::vec4(vDir, 0.0f);
 basis[2] = glm::vec4(nDir, 0.0f);

 glm::mat4 model = glm::translate(glm::mat4(1.0f), corner);
 model = model * basis;
 model = glm::scale(model, glm::vec3(
     static_cast<float>(quadWidth),
     static_cast<float>(quadHeight),
     1.0f));

 FaceInstance fi;
 fi.id = q.id;
 fi.model = model;
 fi.faceIndex = FaceIndexFromGreedy(q.axis, q.faceSign);
 fi.quadSize = glm::vec2(static_cast<float>(quadWidth), static_cast<float>(quadHeight));
 return fi;
}

}

#endif
