#ifndef CROSSMESHEMITTER_H
#define CROSSMESHEMITTER_H

#include "Render/Mesh/GreedyMeshVertex.h"
#include "Blocks/BlockDefinition.h"
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

inline GreedyMeshVertex CrossVertex(const glm::vec3 &pos, float u, float v)
{
  GreedyMeshVertex vtx;
  vtx.px = pos.x;
  vtx.py = pos.y;
  vtx.pz = pos.z;
  vtx.faceIndex = kGreedyCrossFaceIndex;
  vtx.u = u;
  vtx.v = v;
  vtx.skyLight = 1.0f;
  vtx.blockLight = 0.0f;
  vtx.wetness = 0.0f;
  return vtx;
}

/// Two diagonal quads (X-shaped billboard) centered on block center.
inline void AppendCrossSprite(const glm::vec3 &center,
                              std::vector<GreedyMeshVertex> &vertices,
                              std::vector<uint32_t> &indices)
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
  vertices.push_back(CrossVertex(p0, 0.0f, 0.0f));
  vertices.push_back(CrossVertex(p1, 1.0f, 0.0f));
  vertices.push_back(CrossVertex(p2, 1.0f, 1.0f));
  vertices.push_back(CrossVertex(p3, 0.0f, 1.0f));
  indices.push_back(base + 0);
  indices.push_back(base + 1);
  indices.push_back(base + 2);
  indices.push_back(base + 0);
  indices.push_back(base + 2);
  indices.push_back(base + 3);

  const uint32_t base2 = static_cast<uint32_t>(vertices.size());
  vertices.push_back(CrossVertex(q0, 0.0f, 0.0f));
  vertices.push_back(CrossVertex(q1, 1.0f, 0.0f));
  vertices.push_back(CrossVertex(q2, 1.0f, 1.0f));
  vertices.push_back(CrossVertex(q3, 0.0f, 1.0f));
  indices.push_back(base2 + 0);
  indices.push_back(base2 + 1);
  indices.push_back(base2 + 2);
  indices.push_back(base2 + 0);
  indices.push_back(base2 + 2);
  indices.push_back(base2 + 3);
}

inline void AppendDecorQuad(const glm::vec3 &center, float angle,
                            float lateral, float bottom, float width,
                            float height, float lean,
                            std::vector<GreedyMeshVertex> &vertices,
                            std::vector<uint32_t> &indices)
{
  const glm::vec2 tangent(std::cos(angle), std::sin(angle));
  const glm::vec2 origin = glm::vec2(center.x, center.z) +
                           tangent * lateral;
  const glm::vec2 half = tangent * (width * 0.5f);
  const glm::vec2 topCenter = origin + glm::vec2(std::sin(angle),
                                                  -std::cos(angle)) * lean;
  const float y0 = center.y - 0.5f + bottom;
  const float y1 = y0 + height;
  const uint32_t base = static_cast<uint32_t>(vertices.size());
  const auto point3 = [](glm::vec2 xz, float y)
  { return glm::vec3(xz.x, y, xz.y); };
  vertices.push_back(CrossVertex(point3(origin - half, y0), 0.0f, 0.0f));
  vertices.push_back(CrossVertex(point3(origin + half, y0), 1.0f, 0.0f));
  vertices.push_back(CrossVertex(point3(topCenter + half * 0.18f, y1), 0.62f, 1.0f));
  vertices.push_back(CrossVertex(point3(topCenter - half * 0.18f, y1), 0.38f, 1.0f));
  indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

/// Modern plant silhouettes are small clusters of tapered cards, not a single
/// full-height crossed square. Alpha in the authored sprite defines the leaf
/// contour; these templates add height and depth for the selected plant type.
inline void AppendDecorSprite(BlockDecorShape shape, const glm::vec3 &center,
                              std::vector<GreedyMeshVertex> &vertices,
                              std::vector<uint32_t> &indices)
{
  if (shape == BlockDecorShape::Cross)
  {
    AppendCrossSprite(center, vertices, indices);
    return;
  }
  if (shape == BlockDecorShape::Pad)
  {
    // A waterlily is a low horizontal leaf. Its transparent texture defines
    // the rounded edge and notch; the supporting block remains visible below.
    const float half = 0.46f;
    const float y = center.y - 0.48f;
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back(CrossVertex({center.x - half, y, center.z - half}, 0.0f, 0.0f));
    vertices.push_back(CrossVertex({center.x + half, y, center.z - half}, 1.0f, 0.0f));
    vertices.push_back(CrossVertex({center.x + half, y, center.z + half}, 1.0f, 1.0f));
    vertices.push_back(CrossVertex({center.x - half, y, center.z + half}, 0.0f, 1.0f));
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    return;
  }
  if (shape == BlockDecorShape::Tuft)
  {
    static constexpr float kAngles[] = {0.0f, 0.9f, 1.9f, 2.8f, 4.0f, 5.1f};
    static constexpr float kHeights[] = {0.58f, 0.82f, 0.94f, 0.67f, 0.78f, 0.55f};
    for (int i = 0; i < 6; ++i)
    {
      const float lateral = (i % 2 == 0 ? -0.11f : 0.11f);
      AppendDecorQuad(center, kAngles[i], lateral, 0.0f, 0.18f,
                      kHeights[i], (i % 2 == 0 ? -0.08f : 0.08f),
                      vertices, indices);
    }
    return;
  }
  if (shape == BlockDecorShape::Reed)
  {
    static constexpr float kAngles[] = {0.15f, 1.45f, 2.7f, 4.5f};
    static constexpr float kOffsets[] = {-0.20f, -0.07f, 0.08f, 0.20f};
    static constexpr float kHeights[] = {0.72f, 1.20f, 0.94f, 1.05f};
    for (int i = 0; i < 4; ++i)
    {
      AppendDecorQuad(center, kAngles[i], kOffsets[i], 0.0f, 0.14f,
                      kHeights[i], (i % 2 == 0 ? 0.035f : -0.035f),
                      vertices, indices);
    }
    return;
  }
  if (shape == BlockDecorShape::Flame)
  {
    AppendDecorQuad(center, 0.0f, -0.06f, 0.0f, 0.56f, 0.95f, -0.08f,
                    vertices, indices);
    AppendDecorQuad(center, 1.04f, 0.06f, 0.0f, 0.48f, 0.82f, 0.09f,
                    vertices, indices);
    AppendDecorQuad(center, 2.24f, 0.0f, 0.06f, 0.36f, 0.72f, -0.04f,
                    vertices, indices);
    return;
  }
  // Cluster: lower, overlapping foliage/flower cards with varied lean.
  static constexpr float kAngles[] = {0.0f, 1.05f, 2.1f, 3.2f, 4.25f, 5.3f};
  static constexpr float kOffsets[] = {-0.17f, 0.15f, -0.10f, 0.18f, 0.0f, -0.14f};
  static constexpr float kHeights[] = {0.60f, 0.72f, 0.85f, 0.67f, 0.94f, 0.76f};
  for (int i = 0; i < 6; ++i)
  {
    AppendDecorQuad(center, kAngles[i], kOffsets[i], 0.0f, 0.30f,
                    kHeights[i], (i % 2 == 0 ? -0.07f : 0.06f),
                    vertices, indices);
  }
}

} // namespace cutum

#endif
