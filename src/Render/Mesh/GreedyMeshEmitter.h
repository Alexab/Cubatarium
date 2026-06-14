#ifndef GREEDYMESHEMITTER_H
#define GREEDYMESHEMITTER_H

#include "World/Chunks/Chunk.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "Render/Mesh/GreedyMesher.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

namespace
{

inline int FaceIndexFromGreedy(int axis, int faceSign)
{
  if (axis == 2)
  {
    return faceSign > 0 ? 0 : 2;
  }
  if (axis == 0)
  {
    return faceSign > 0 ? 1 : 3;
  }
  return faceSign > 0 ? 4 : 5;
}

inline GreedyMeshVertex MakeVertex(const glm::vec3 &pos, int faceIndex)
{
  GreedyMeshVertex v;
  v.px = pos.x;
  v.py = pos.y;
  v.pz = pos.z;
  v.faceIndex = static_cast<float>(faceIndex);
  v.u = 0.0f;
  v.v = 0.0f;
  return v;
}

} // namespace

inline void AppendGreedyQuad(const GreedyQuad &q, glm::ivec3 chunkCoord,
                             std::vector<GreedyMeshVertex> &vertices,
                             std::vector<uint32_t> &indices)
{
  const int uAxis = (q.axis + 1) % 3;
  const int vAxis = (q.axis + 2) % 3;

  glm::vec3 uDir(0.0f);
  glm::vec3 vDir(0.0f);
  glm::vec3 nDir(0.0f);
  uDir[uAxis] = 1.0f;
  vDir[vAxis] = 1.0f;
  nDir[q.axis] = static_cast<float>(q.faceSign);

  const float width = static_cast<float>(q.width);
  const float height = static_cast<float>(q.height);

  const glm::vec3 chunkOrigin(static_cast<float>(chunkCoord.x * CHUNK_SIZE),
                              static_cast<float>(chunkCoord.y * CHUNK_SIZE),
                              static_cast<float>(chunkCoord.z * CHUNK_SIZE));

  glm::vec3 corner = chunkOrigin;
  corner[q.axis] +=
      static_cast<float>(q.slice) + (q.faceSign > 0 ? 0.5f : -0.5f);
  corner[uAxis] += static_cast<float>(q.u) - 0.5f;
  corner[vAxis] += static_cast<float>(q.v) - 0.5f;
  const glm::vec3 p0 = corner;
  const glm::vec3 p1 = corner + uDir * width;
  const glm::vec3 p2 = corner + uDir * width + vDir * height;
  const glm::vec3 p3 = corner + vDir * height;

  const int faceIndex = FaceIndexFromGreedy(q.axis, q.faceSign);
  const uint32_t base = static_cast<uint32_t>(vertices.size());
  vertices.push_back(MakeVertex(p0, faceIndex));
  vertices.push_back(MakeVertex(p1, faceIndex));
  vertices.push_back(MakeVertex(p2, faceIndex));
  vertices.push_back(MakeVertex(p3, faceIndex));

  const bool flipWinding = glm::dot(glm::cross(uDir, vDir), nDir) < 0.0f;
  if (flipWinding)
  {
    indices.push_back(base + 0);
    indices.push_back(base + 3);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 1);
  }
  else
  {
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
  }
}

} // namespace cutum

#endif
