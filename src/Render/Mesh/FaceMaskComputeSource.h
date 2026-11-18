#pragma once
#include "World/Mesh/Contracts/CubeFaceVisibility.h"
#include <string>
namespace cutum
{
inline std::string FaceMaskComputeSource(bool gles)
{
  const std::string version =
      gles ? "#version 310 es\nprecision highp float;\nprecision highp int;\n"
           : "#version 430\n";
  return version + std::string(R"(layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Occ { uint cells[]; };
layout(std430, binding = 1) writeonly buffer Mask { uint mask[]; };
uniform uint volume;
uniform uint side;
uniform uint pad;
)") + CubeFaceVisibilityGlsl() +
         R"(
uint cellAt(ivec3 p) {
  return cells[(p.y * int(pad) + p.z) * int(pad) + p.x];
}
bool visibleFace(ivec3 p, ivec3 direction) {
  uint decision = cubeFaceDecision(cellAt(p), cellAt(p + direction));
  return decision == 1u;
}
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= volume) return;
  int n = int(side);
  ivec3 p = ivec3(int(i) % n, int(i) / (n*n), (int(i) / n) % n) + int(FACE_HALO);
  if ((cellAt(p) & FACE_ELIGIBLE) == 0u) { mask[i] = 0u; return; }
  uint m = 0u;
  if (visibleFace(p, ivec3(-1,0,0))) m |= 1u;
  if (visibleFace(p, ivec3(1,0,0))) m |= 2u;
  if (visibleFace(p, ivec3(0,-1,0))) m |= 4u;
  if (visibleFace(p, ivec3(0,1,0))) m |= 8u;
  if (visibleFace(p, ivec3(0,0,-1))) m |= 16u;
  if (visibleFace(p, ivec3(0,0,1))) m |= 32u;
  mask[i] = m;
}
)";
}
} // namespace cutum
