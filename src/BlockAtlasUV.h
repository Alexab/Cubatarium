#ifndef BLOCKATLASUV_H
#define BLOCKATLASUV_H

#include <glm/glm.hpp>

namespace cutum {

inline glm::vec3 FaceNormalFromIndex(int faceIndex)
{
 switch (faceIndex) {
 case 0: return glm::vec3(0.0f, 0.0f, 1.0f);
 case 1: return glm::vec3(1.0f, 0.0f, 0.0f);
 case 2: return glm::vec3(0.0f, 0.0f, -1.0f);
 case 3: return glm::vec3(-1.0f, 0.0f, 0.0f);
 case 4: return glm::vec3(0.0f, 1.0f, 0.0f);
 default: return glm::vec3(0.0f, -1.0f, 0.0f);
 }
}

inline glm::vec3 BlockCenterFromFacePoint(const glm::vec3& worldPos, const glm::vec3& normal)
{
 const glm::vec3 inside = worldPos - normal * 1.0e-3f;
 return glm::floor(inside + glm::vec3(0.5f));
}

/// Atlas UV for a point on a cube face; matches InitCubeBuffers / CubeGL layout.
inline glm::vec2 AtlasUVFromWorldPos(int faceIndex, const glm::vec3& worldPos)
{
 constexpr float kCubeShift = 1.0f / 6.0f;
 const float u0 = static_cast<float>(faceIndex) * kCubeShift;
 const float u1 = static_cast<float>(faceIndex + 1) * kCubeShift;

 const glm::vec3 normal = FaceNormalFromIndex(faceIndex);
 const glm::vec3 blockCenter = BlockCenterFromFacePoint(worldPos, normal);
 const glm::vec3 local = worldPos - blockCenter;

 switch (faceIndex) {
 case 0: // +Z NEAR
  return glm::vec2(glm::mix(u0, u1, local.x + 0.5f), glm::mix(1.0f, 0.0f, local.y + 0.5f));
 case 1: // +X RIGHT
  return glm::vec2(glm::mix(u0, u1, 0.5f - local.z), glm::mix(1.0f, 0.0f, local.y + 0.5f));
 case 2: // -Z FAR
  return glm::vec2(glm::mix(u0, u1, 0.5f - local.x), glm::mix(1.0f, 0.0f, local.y + 0.5f));
 case 3: // -X LEFT
  return glm::vec2(glm::mix(u0, u1, local.z + 0.5f), glm::mix(1.0f, 0.0f, local.y + 0.5f));
 case 4: // +Y TOP
  return glm::vec2(glm::mix(u0, u1, local.x + 0.5f), glm::mix(0.0f, 1.0f, 0.5f - local.z));
 default: // -Y BOTTOM
  return glm::vec2(glm::mix(u0, u1, local.x + 0.5f), glm::mix(0.0f, 1.0f, local.z + 0.5f));
 }
}

}

#endif
