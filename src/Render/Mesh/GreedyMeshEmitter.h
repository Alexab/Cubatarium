#ifndef GREEDYMESHEMITTER_H
#define GREEDYMESHEMITTER_H

#include "Render/Mesh/GreedyMeshCommon.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "Render/Mesh/GreedyMesher.h"
#include "World/Chunks/Chunk.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

// Minecraft-style fluid surface height (W4, TD-FL-012): a source cell's
// surface sits at 14/16 of the block; flowing levels thin out with the
// level; falling columns and submerged cells keep the full cube (the
// caller gates truncation on GreedyQuad::FluidSurface).
inline float FluidCellHeight(uint8_t packed)
{
  const uint8_t level = packed & 0x7u;         // bits 0-2: Level (0=source)
  const bool falling = (packed & 0x8u) != 0;   // bit 3: Falling
  if (falling)
  {
    return 1.0f;
  }
  if (level == 0)
  {
    return 0.875f; // 14/16 source surface
  }
  const float h = 0.875f * static_cast<float>(8 - level) / 8.0f;
  return h < 0.125f ? 0.125f : h;
}

// Vertical inset of a truncated fluid surface cell.
inline float FluidSurfaceInset(uint8_t packed)
{
  return 1.0f - FluidCellHeight(packed);
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
  v.skyLight = 1.0f;
  v.blockLight = 0.0f;
  v.wetness = 0.0f;
  return v;
}

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

  // Minecraft-style fluid surface (W4): a surface fluid cell (air above)
  // renders its top below the block top — 14/16 for a source. Top faces
  // only exist at the surface; side faces lower their top edge so shores
  // and terrace steps do not read as full dark walls. FluidSurface==2 is a
  // legacy kindless source: assume the source height.
  const float fluid_inset =
      q.FluidSurface == 2
          ? 1.0f - 0.875f
          : (q.FluidSurface == 1 && q.FluidPacked != 0
                 ? FluidSurfaceInset(q.FluidPacked)
                 : 0.0f);
  if (fluid_inset > 0.0f && q.axis == 1 && q.faceSign > 0)
  {
    corner[1] -= fluid_inset;
  }

  glm::vec3 p0 = corner;
  glm::vec3 p1 = corner + uDir * width;
  glm::vec3 p2 = corner + uDir * width + vDir * height;
  glm::vec3 p3 = corner + vDir * height;

  // Side faces of surface fluid cells: lower the two top-edge vertices by
  // the surface inset (the quad is axis-aligned, so the top edge is the
  // pair of vertices sharing the maximum Y).
  if (fluid_inset > 0.0f && q.axis != 1)
  {
    const float max_y = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
    glm::vec3 *pts[4] = {&p0, &p1, &p2, &p3};
    for (glm::vec3 *p : pts)
    {
      if (std::abs(p->y - max_y) < 1e-6f)
      {
        p->y -= fluid_inset;
      }
    }
  }

  const int faceIndex = FaceIndexFromGreedy(q.axis, q.faceSign);
  const uint32_t base = static_cast<uint32_t>(vertices.size());
  vertices.push_back(MakeVertex(p0, faceIndex));
  vertices.push_back(MakeVertex(p1, faceIndex));
  vertices.push_back(MakeVertex(p2, faceIndex));
  vertices.push_back(MakeVertex(p3, faceIndex));

  const bool flipWinding = glm::dot(glm::cross(uDir, vDir), nDir) < 0.0f;
  auto pushTri = [&](uint32_t a, uint32_t b, uint32_t c)
  {
    indices.push_back(a);
    indices.push_back(b);
    indices.push_back(c);
  };
  if (flipWinding)
  {
    pushTri(base + 0, base + 3, base + 2);
    pushTri(base + 0, base + 2, base + 1);
  }
  else
  {
    pushTri(base + 0, base + 1, base + 2);
    pushTri(base + 0, base + 2, base + 3);
  }
  // Horizontal fluid faces are visible from inside the volume (e.g. surface
  // when submerged); shell pass disables cull but backfaces still miss stencil.
  // Below-surface column tint is opaque-only (see
  // ShouldApplyBelowSurfaceFogToPass).
  if (q.FluidPacked != 0 && q.axis == 1)
  {
    if (flipWinding)
    {
      pushTri(base + 0, base + 1, base + 2);
      pushTri(base + 0, base + 2, base + 3);
    }
    else
    {
      pushTri(base + 0, base + 3, base + 2);
      pushTri(base + 0, base + 2, base + 1);
    }
  }
}

} // namespace cutum

#endif
