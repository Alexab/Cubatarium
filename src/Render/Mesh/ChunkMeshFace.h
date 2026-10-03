#pragma once

#include <glm/glm.hpp>

namespace cutum
{

/// Face-mask order shared by ChunkMeshSnapshot::BoundaryOverlayState:
/// -X, +X, -Y, +Y, -Z, +Z. Keep every face-mask consumer in this domain.
inline glm::ivec3 ChunkMeshFaceNeighborDelta(int face)
{
  switch (face)
  {
  case 0:
    return {-1, 0, 0};
  case 1:
    return {1, 0, 0};
  case 2:
    return {0, -1, 0};
  case 3:
    return {0, 1, 0};
  case 4:
    return {0, 0, -1};
  case 5:
    return {0, 0, 1};
  default:
    return {0, 0, 0};
  }
}

inline int ChunkMeshFaceIndexForDelta(glm::ivec3 delta)
{
  for (int face = 0; face < 6; ++face)
  {
    const glm::ivec3 face_delta = ChunkMeshFaceNeighborDelta(face);
    if (face_delta.x == delta.x && face_delta.y == delta.y &&
        face_delta.z == delta.z)
    {
      return face;
    }
  }
  return -1;
}

} // namespace cutum
