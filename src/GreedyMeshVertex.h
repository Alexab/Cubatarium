#ifndef GREEDYMESHVERTEX_H
#define GREEDYMESHVERTEX_H

namespace cutum
{

/// faceIndex 0–5: greedy cube faces; kGreedyCrossFaceIndex: explicit mesh UVs.
constexpr float kGreedyCrossFaceIndex = 127.0f;

struct GreedyMeshVertex
{
  float px;
  float py;
  float pz;
  float faceIndex;
  float u;
  float v;
};

} // namespace cutum

#endif
