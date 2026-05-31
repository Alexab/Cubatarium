#ifndef CROSSMESHEMITTER_H
#define CROSSMESHEMITTER_H

#include "GreedyMeshVertex.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace cutum {

inline GreedyMeshVertex CrossVertex(const glm::vec3& pos)
{
 GreedyMeshVertex v;
 v.px = pos.x;
 v.py = pos.y;
 v.pz = pos.z;
 v.faceIndex = 4.0f;
 return v;
}

/// Two diagonal quads (X-shaped billboard) centered on block center.
inline void AppendCrossSprite(const glm::vec3& center,
                              std::vector<GreedyMeshVertex>& vertices,
                              std::vector<uint32_t>& indices)
{
 const float hx = 0.5f;
 const float hy = 0.5f;
 const glm::vec3 p0(center.x - hx, center.y - hy, center.z - hx);
 const glm::vec3 p1(center.x + hx, center.y - hy, center.z + hx);
 const glm::vec3 p2(center.x + hx, center.y + hy, center.z + hx);
 const glm::vec3 p3(center.x - hx, center.y + hy, center.z - hx);

 const glm::vec3 q0(center.x - hx, center.y - hy, center.z + hx);
 const glm::vec3 q1(center.x + hx, center.y - hy, center.z - hx);
 const glm::vec3 q2(center.x + hx, center.y + hy, center.z - hx);
 const glm::vec3 q3(center.x - hx, center.y + hy, center.z + hx);

 const uint32_t base = static_cast<uint32_t>(vertices.size());
 vertices.push_back(CrossVertex(p0));
 vertices.push_back(CrossVertex(p1));
 vertices.push_back(CrossVertex(p2));
 vertices.push_back(CrossVertex(p3));
 indices.push_back(base + 0);
 indices.push_back(base + 1);
 indices.push_back(base + 2);
 indices.push_back(base + 0);
 indices.push_back(base + 2);
 indices.push_back(base + 3);

 const uint32_t base2 = static_cast<uint32_t>(vertices.size());
 vertices.push_back(CrossVertex(q0));
 vertices.push_back(CrossVertex(q1));
 vertices.push_back(CrossVertex(q2));
 vertices.push_back(CrossVertex(q3));
 indices.push_back(base2 + 0);
 indices.push_back(base2 + 1);
 indices.push_back(base2 + 2);
 indices.push_back(base2 + 0);
 indices.push_back(base2 + 2);
 indices.push_back(base2 + 3);
}

} // namespace cutum

#endif
