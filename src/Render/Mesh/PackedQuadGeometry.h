#pragma once
#include "Render/Mesh/PackedQuad.h"
#include <array>
#include <glm/glm.hpp>

namespace cutum
{
/// Reconstruct the vertex-pulling shader's four corners from actual SSBO words.
/// Reject malformed cell/extent/face data rather than reporting a false match.
inline bool DecodePackedQuadCorners(const PackedQuad &q,
                                    glm::ivec3 chunk_origin,
                                    std::array<glm::vec3, 4> &corners)
{
  const int face = q.Face();
  if (face < 0 || face > 5 || q.W() == 0 || q.H() == 0 || q.X() >= 16 ||
      q.Y() >= 16 || q.Z() >= 16)
    return false;
  const int axis = face == 1 || face == 3 ? 0 : (face >= 4 ? 1 : 2);
  const glm::ivec3 cell(q.X(), q.Y(), q.Z());
  if (cell[(axis + 1) % 3] + q.W() > 16 || cell[(axis + 2) % 3] + q.H() > 16)
    return false;
  glm::vec3 pos = glm::vec3(chunk_origin) + glm::vec3(cell) - glm::vec3(0.5f);
  glm::vec3 du, dv;
  switch (face)
  {
  case 0:
    pos.z += 1;
    du = {1, 0, 0};
    dv = {0, 1, 0};
    break;
  case 1:
    pos.x += 1;
    du = {0, 1, 0};
    dv = {0, 0, 1};
    break;
  case 2:
    pos.x += q.W();
    du = {-1, 0, 0};
    dv = {0, 1, 0};
    break;
  case 3:
    pos.y += q.W();
    du = {0, -1, 0};
    dv = {0, 0, 1};
    break;
  case 4:
    pos.y += 1;
    du = {0, 0, 1};
    dv = {1, 0, 0};
    break;
  default:
    pos.z += q.W();
    du = {0, 0, -1};
    dv = {1, 0, 0};
    break;
  }
  corners = {pos, pos + du * float(q.W()),
             pos + du * float(q.W()) + dv * float(q.H()),
             pos + dv * float(q.H())};
  return true;
}
} // namespace cutum
