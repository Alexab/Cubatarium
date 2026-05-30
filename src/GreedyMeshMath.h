#ifndef GREEDYMESHMATH_H
#define GREEDYMESHMATH_H

#include "Chunk.h"
#include "ChunkMeshCache.h"
#include "GreedyMesher.h"
#include <glm/gtc/matrix_transform.hpp>

namespace cutum {

namespace {

constexpr float kFaceEpsilon = 0.002f;

inline glm::vec4 AtlasUVForFace(int axis, int faceSign)
{
 const float s = 1.0f / 6.0f;
 int faceIndex = 0;
 bool flipV = true;
 if (axis == 2) {
  faceIndex = faceSign > 0 ? 0 : 2;
 } else if (axis == 0) {
  faceIndex = faceSign > 0 ? 1 : 3;
 } else {
  faceIndex = faceSign > 0 ? 4 : 5;
  flipV = false;
 }
 const float u0 = static_cast<float>(faceIndex) * s;
 const float u1 = static_cast<float>(faceIndex + 1) * s;
 if (flipV) {
  return glm::vec4(u0, 1.0f, u1, 0.0f);
 }
 return glm::vec4(u0, 0.0f, u1, 1.0f);
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

 int quadWidth = q.width;
 int quadHeight = q.height;
 if (glm::dot(glm::cross(uDir, vDir), nDir) < 0.0f) {
  std::swap(uDir, vDir);
  std::swap(quadWidth, quadHeight);
 }

 const glm::vec3 chunkOrigin(
     chunkCoord.x * CHUNK_SIZE,
     chunkCoord.y * CHUNK_SIZE,
     chunkCoord.z * CHUNK_SIZE);

 glm::vec3 corner = chunkOrigin;
 corner[q.axis] = static_cast<float>(q.slice) + (q.faceSign > 0 ? 1.0f : 0.0f);
 corner[uAxis] = static_cast<float>(q.u);
 corner[vAxis] = static_cast<float>(q.v);

 const glm::vec3 center = corner
     + uDir * (static_cast<float>(quadWidth) * 0.5f)
     + vDir * (static_cast<float>(quadHeight) * 0.5f)
     + nDir * kFaceEpsilon;

 glm::mat4 R(1.0f);
 R[0] = glm::vec4(uDir, 0.0f);
 R[1] = glm::vec4(vDir, 0.0f);
 R[2] = glm::vec4(nDir, 0.0f);

 glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
 model = model * R;
 model = glm::scale(model, glm::vec3(
     static_cast<float>(quadWidth),
     static_cast<float>(quadHeight),
     1.0f));

 FaceInstance fi;
 fi.id = q.id;
 fi.model = model;
 fi.atlasUV = AtlasUVForFace(q.axis, q.faceSign);
 fi.quadSize = glm::vec2(static_cast<float>(quadWidth), static_cast<float>(quadHeight));
 return fi;
}

}

#endif
