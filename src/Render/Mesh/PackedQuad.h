#pragma once

#include <cstdint>

namespace cutum
{

/// 8-byte packed quad for GPU-resident vertex pulling.
/// word0: x(5) | y(5) | z(5) | w(5) | h(5) | face(3) | flags_lo(4)
/// word1: blockType(10) | skyLight(4) | blockLight(4) | ao(4) | fluid(2) | flags_hi(8)
///
/// Vertex shader reconstructs 4 corners from gl_VertexID:
///   quadIndex = gl_VertexID / 6, corner = gl_VertexID % 6 (two triangles).
///   Chunk origin from per-draw push constant or SSBO.
struct PackedQuad
{
  uint32_t word0;
  uint32_t word1;

  static PackedQuad Encode(int x, int y, int z, int w, int h, int face,
                           int blockType, int skyLight, int blockLight,
                           int ao = 0, int fluid = 0, int flags = 0)
  {
    PackedQuad q;
    q.word0 = (static_cast<uint32_t>(x & 0x1F))
            | (static_cast<uint32_t>(y & 0x1F) << 5)
            | (static_cast<uint32_t>(z & 0x1F) << 10)
            | (static_cast<uint32_t>(w & 0x1F) << 15)
            | (static_cast<uint32_t>(h & 0x1F) << 20)
            | (static_cast<uint32_t>(face & 0x7) << 25)
            | (static_cast<uint32_t>(flags & 0xF) << 28);
    q.word1 = (static_cast<uint32_t>(blockType & 0x3FF))
            | (static_cast<uint32_t>(skyLight & 0xF) << 10)
            | (static_cast<uint32_t>(blockLight & 0xF) << 14)
            | (static_cast<uint32_t>(ao & 0xF) << 18)
            | (static_cast<uint32_t>(fluid & 0x3) << 22)
            | (static_cast<uint32_t>(flags >> 4) << 24);
    return q;
  }

  int X() const { return static_cast<int>(word0 & 0x1F); }
  int Y() const { return static_cast<int>((word0 >> 5) & 0x1F); }
  int Z() const { return static_cast<int>((word0 >> 10) & 0x1F); }
  int W() const { return static_cast<int>((word0 >> 15) & 0x1F); }
  int H() const { return static_cast<int>((word0 >> 20) & 0x1F); }
  int Face() const { return static_cast<int>((word0 >> 25) & 0x7); }
  int BlockType() const { return static_cast<int>(word1 & 0x3FF); }
  int SkyLight() const { return static_cast<int>((word1 >> 10) & 0xF); }
  int BlockLight() const { return static_cast<int>((word1 >> 14) & 0xF); }
};

/// GLSL source for the vertex pulling shader that unpacks PackedQuad from an
/// SSBO and emits world-space position + face/light attributes.
inline constexpr const char *kPackedQuadVertexPullingGlsl = R"(#version 430
layout(std430, binding = 0) readonly buffer QuadBuf { uvec2 quads[]; };

uniform vec3 chunkOrigin;
uniform mat4 mvp;

out flat int vFace;
out float vSkyLight;
out float vBlockLight;

void main() {
  int quadIdx = gl_VertexID / 6;
  int corner  = gl_VertexID % 6;

  uvec2 q = quads[quadIdx];
  uint w0 = q.x, w1 = q.y;

  float x = float(w0 & 0x1Fu);
  float y = float((w0 >> 5u) & 0x1Fu);
  float z = float((w0 >> 10u) & 0x1Fu);
  float qw = float((w0 >> 15u) & 0x1Fu);
  float qh = float((w0 >> 20u) & 0x1Fu);
  int face = int((w0 >> 25u) & 0x7u);

  float sky = float((w1 >> 10u) & 0xFu) / 15.0;
  float blk = float((w1 >> 14u) & 0xFu) / 15.0;

  // Face-indexed axis table:
  // face 0: +Z (front)   1: +X (right)  2: -Z (back)
  //      3: -X (left)    4: +Y (top)    5: -Y (bottom)
  // Axis normal, U tangent, V tangent:
  vec3 pos = vec3(x, y, z);
  vec3 du, dv;

  if (face == 0)      { pos.z += 1.0; du = vec3(1,0,0); dv = vec3(0,1,0); }
  else if (face == 1) { pos.x += 1.0; du = vec3(0,0,1); dv = vec3(0,1,0); }
  else if (face == 2) { du = vec3(-1,0,0); dv = vec3(0,1,0); pos.x += qw; }
  else if (face == 3) { du = vec3(0,0,-1); dv = vec3(0,1,0); pos.z += qw; }
  else if (face == 4) { pos.y += 1.0; du = vec3(1,0,0); dv = vec3(0,0,1); }
  else                { du = vec3(1,0,0); dv = vec3(0,0,-1); pos.z += qh; }

  // 6 vertices per quad: 0-1-2, 0-2-3 (two triangles)
  // corners: 0=origin, 1=+du*w, 2=+du*w+dv*h, 3=+dv*h
  vec3 offset;
  if      (corner == 0) offset = vec3(0);
  else if (corner == 1 || corner == 3) offset = du * qw;
  else if (corner == 2 || corner == 4) offset = du * qw + dv * qh;
  else                  offset = dv * qh;

  vec3 worldPos = chunkOrigin + pos + offset;
  gl_Position = mvp * vec4(worldPos, 1.0);
  vFace = face;
  vSkyLight = sky;
  vBlockLight = blk;
}
)";

} // namespace cutum
