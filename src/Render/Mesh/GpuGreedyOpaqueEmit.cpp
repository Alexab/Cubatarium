#include "Render/Mesh/GpuGreedyOpaqueEmit.h"
#include "Render/Mesh/GpuGreedyFaceExtract.h"
#include "Render/GlIncludes.h"
#include "World/Chunks/Chunk.h"
#include "glog/logging.h"
#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace cutum
{
namespace
{

uint64_t gOpaqueEmitGpu = 0;

constexpr uint32_t kMaxGpuRects = 16384u;
constexpr uint32_t kGpuPlaneWorkgroups = 102u;
constexpr int kGpuVertFloats = 10;

#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)

const char *kMaskCompute = R"(#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Occ { uint occ[]; };
layout(std430, binding = 1) writeonly buffer Mask { uint mask[]; };
uniform uint volume;
uniform uint side;
uniform uint pad;
uint occAtPad(int px, int py, int pz) {
  uint i = uint((py * int(pad) + pz) * int(pad) + px);
  uint word = occ[i >> 2];
  return (word >> ((i & 3u) * 8u)) & 0xffu;
}
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= volume) return;
  int side_i = int(side);
  int x = int(i % uint(side_i));
  int y = int(i / uint(side_i * side_i));
  int z = int((i / uint(side_i)) % uint(side_i));
  int px = x + 1, py = y + 1, pz = z + 1;
  if (occAtPad(px, py, pz) == 0u) { mask[i] = 0u; return; }
  uint m = 0u;
  if (occAtPad(px - 1, py, pz) == 0u) m |= 1u;
  if (occAtPad(px + 1, py, pz) == 0u) m |= 2u;
  if (occAtPad(px, py - 1, pz) == 0u) m |= 4u;
  if (occAtPad(px, py + 1, pz) == 0u) m |= 8u;
  if (occAtPad(px, py, pz - 1) == 0u) m |= 16u;
  if (occAtPad(px, py, pz + 1) == 0u) m |= 32u;
  mask[i] = m;
}
)";

const char *kGreedyRectCompute = R"(#version 430
layout(local_size_x = 1) in;
layout(std430, binding = 0) readonly buffer Mask { uint mask[]; };
layout(std430, binding = 1) readonly buffer Blocks { uint blocks[]; };
layout(std430, binding = 2) readonly buffer Lights { uint lights[]; };
struct GpuRect {
  uint axis, faceSign, slice, u, v, width, height, blockId, lightPacked;
};
layout(std430, binding = 3) buffer Rects { GpuRect rects[]; };
layout(std430, binding = 4) buffer Counters { uint rectCount; };
uniform uint side;
uint readBlock(uint index) {
  uint word = blocks[index >> 2];
  return (word >> ((index & 3u) * 8u)) & 0xffu;
}
uint readLight(uint index) {
  uint word = lights[index >> 2];
  return (word >> ((index & 3u) * 8u)) & 0xffu;
}
uint cellValue(int axis, int faceSign, int slice, int u, int v) {
  ivec3 local;
  local[axis] = faceSign > 0 ? slice - 1 : slice;
  int uAxis = (axis + 1) % 3, vAxis = (axis + 2) % 3;
  local[uAxis] = u; local[vAxis] = v;
  if (local[axis] < 0 || local[axis] >= int(side)) return 0u;
  int li = (local.y * int(side) + local.z) * int(side) + local.x;
  uint bit = uint(axis) * 2u + (faceSign > 0 ? 1u : 0u);
  if ((mask[li] & (1u << bit)) == 0u) return 0u;
  uint blockId = readBlock(uint(li));
  if (blockId == 0u) return 0u;
  return blockId | (readLight(uint(li)) << 8u);
}
void main() {
  uint plane = gl_WorkGroupID.x;
  if (plane >= 102u) return;
  uint axis = plane / 34u, rem = plane % 34u;
  uint signIdx = rem / 17u, slice = rem % 17u;
  int faceSign = signIdx == 0u ? -1 : 1;
  uint grid[256];
  for (int i = 0; i < 256; ++i) grid[i] = 0u;
  for (int v = 0; v < 16; ++v)
    for (int u = 0; u < 16; ++u)
      grid[v * 16 + u] = cellValue(int(axis), faceSign, int(slice), u, v);
  for (int v = 0; v < 16; ++v) {
    for (int u = 0; u < 16; ++u) {
      uint val = grid[v * 16 + u];
      if (val == 0u) continue;
      int width = 1;
      while (u + width < 16 && grid[v * 16 + u + width] == val) ++width;
      int height = 1; bool grow = true;
      while (grow && v + height < 16) {
        for (int du = 0; du < width; ++du)
          if (grid[(v + height) * 16 + u + du] != val) { grow = false; break; }
        if (grow) ++height;
      }
      for (int dv = 0; dv < height; ++dv)
        for (int du = 0; du < width; ++du)
          grid[(v + dv) * 16 + u + du] = 0u;
      uint idx = atomicAdd(rectCount, 1u);
      if (idx >= 16384u) return;
      rects[idx].axis = axis; rects[idx].faceSign = signIdx;
      rects[idx].slice = slice; rects[idx].u = uint(u); rects[idx].v = uint(v);
      rects[idx].width = uint(width); rects[idx].height = uint(height);
      rects[idx].blockId = val & 0xffu; rects[idx].lightPacked = (val >> 8u) & 0xffu;
    }
  }
}
)";

const char *kVertexEmitCompute = R"(#version 430
layout(local_size_x = 64) in;
struct GpuRect {
  uint axis, faceSign, slice, u, v, width, height, blockId, lightPacked;
};
layout(std430, binding = 0) readonly buffer Rects { GpuRect rects[]; };
layout(std430, binding = 1) writeonly buffer Vertices { float verts[]; };
layout(std430, binding = 2) writeonly buffer Indices { uint inds[]; };
uniform uint numRects;
uniform ivec3 chunkCoord;
uniform uint side;
int faceIndexFromGreedy(int axis, int faceSign) {
  if (axis == 2) return faceSign > 0 ? 0 : 2;
  if (axis == 0) return faceSign > 0 ? 1 : 3;
  return faceSign > 0 ? 4 : 5;
}
void writeVert(uint base, uint slot, vec3 p, int fi, float sky, float block) {
  uint o = (base + slot) * 10u;
  verts[o]=p.x; verts[o+1u]=p.y; verts[o+2u]=p.z; verts[o+3u]=float(fi);
  verts[o+4u]=0.0; verts[o+5u]=0.0; verts[o+6u]=sky; verts[o+7u]=block;
  verts[o+8u]=0.0; verts[o+9u]=0.0;
}
void main() {
  uint rid = gl_GlobalInvocationID.x;
  if (rid >= numRects) return;
  GpuRect r = rects[rid];
  int axis = int(r.axis), faceSign = r.faceSign == 1u ? 1 : -1;
  int uAxis = (axis + 1) % 3, vAxis = (axis + 2) % 3;
  float width = float(r.width), height = float(r.height);
  vec3 chunkOrigin = vec3(chunkCoord) * float(side);
  vec3 corner = vec3(0.0);
  corner[axis] = float(r.slice) + (faceSign > 0 ? 0.5 : -0.5);
  corner[uAxis] = float(r.u) - 0.5; corner[vAxis] = float(r.v) - 0.5;
  corner += chunkOrigin;
  vec3 uDir=vec3(0), vDir=vec3(0), nDir=vec3(0);
  uDir[uAxis]=1.0; vDir[vAxis]=1.0; nDir[axis]=float(faceSign);
  vec3 p0=corner, p1=corner+uDir*width, p2=corner+uDir*width+vDir*height, p3=corner+vDir*height;
  int fi = faceIndexFromGreedy(axis, faceSign);
  float sky=float(r.lightPacked&0x0Fu)/15.0, block=float((r.lightPacked>>4u)&0x0Fu)/15.0;
  uint vbase = rid * 4u;
  writeVert(vbase,0u,p0,fi,sky,block); writeVert(vbase,1u,p1,fi,sky,block);
  writeVert(vbase,2u,p2,fi,sky,block); writeVert(vbase,3u,p3,fi,sky,block);
  bool flip = dot(cross(uDir,vDir),nDir) < 0.0;
  uint ibase = rid * 6u;
  if (flip) {
    inds[ibase]=vbase; inds[ibase+1u]=vbase+3u; inds[ibase+2u]=vbase+2u;
    inds[ibase+3u]=vbase; inds[ibase+4u]=vbase+2u; inds[ibase+5u]=vbase+1u;
  } else {
    inds[ibase]=vbase; inds[ibase+1u]=vbase+1u; inds[ibase+2u]=vbase+2u;
    inds[ibase+3u]=vbase; inds[ibase+4u]=vbase+2u; inds[ibase+5u]=vbase+3u;
  }
}
)";

GLuint CompileCompute(const char *src, const char *label)
{
  const GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[512];
    glGetShaderInfoLog(sh, 512, nullptr, log);
    LOG(WARNING) << "[GpuGreedyOpaqueEmit] " << label << " compile failed: "
                 << log;
    glDeleteShader(sh);
    return 0;
  }
  const GLuint prog = glCreateProgram();
  glAttachShader(prog, sh);
  glLinkProgram(prog);
  glDeleteShader(sh);
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    LOG(WARNING) << "[GpuGreedyOpaqueEmit] " << label << " link failed";
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

void PackBytes(const uint8_t *bytes, size_t count, std::vector<uint32_t> &out)
{
  out.assign((count + 3) / 4, 0);
  for (size_t i = 0; i < count; ++i)
  {
    out[i >> 2] |= static_cast<uint32_t>(bytes[i]) << ((i & 3u) * 8u);
  }
}

#endif

} // namespace

bool EnsureGpuOpaqueEmit(GpuGreedyEmitState &state)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)state;
  return false;
#else
  if (state.InitAttempted)
  {
    return state.MaskProgram != 0 && state.GreedyProgram != 0 &&
           state.EmitProgram != 0;
  }
  state.InitAttempted = true;
  state.MaskProgram = CompileCompute(kMaskCompute, "mask");
  state.GreedyProgram = CompileCompute(kGreedyRectCompute, "greedy");
  state.EmitProgram = CompileCompute(kVertexEmitCompute, "emit");
  if (!state.MaskProgram || !state.GreedyProgram || !state.EmitProgram)
  {
    return false;
  }
  glGenBuffers(1, &state.OccSsbo);
  glGenBuffers(1, &state.MaskSsbo);
  glGenBuffers(1, &state.BlocksSsbo);
  glGenBuffers(1, &state.LightsSsbo);
  glGenBuffers(1, &state.RectsSsbo);
  glGenBuffers(1, &state.CountersSsbo);
  glGenBuffers(1, &state.VertSsbo);
  glGenBuffers(1, &state.IndexSsbo);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.RectsSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(kMaxGpuRects * 9 * sizeof(uint32_t)),
               nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.VertSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(kMaxGpuRects * 4 * kGpuVertFloats *
                                       sizeof(float)),
               nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.IndexSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(kMaxGpuRects * 6 * sizeof(uint32_t)),
               nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  return true;
#endif
}

bool TryGpuOpaqueEmitToBatches(GpuGreedyEmitState &state,
                               const ChunkMeshSnapshot &snapshot,
                               UBlockRegistry &registry, glm::ivec3 coord,
                               std::vector<GreedyMeshBatch> &out_batches)
{
  out_batches.clear();
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)state;
  (void)snapshot;
  (void)registry;
  (void)coord;
  return false;
#else
#if defined(_WIN32)
  if (wglGetCurrentContext() == nullptr)
  {
    return false;
  }
#endif
  if (!SnapshotIsGpuExtractEligible(snapshot, registry) ||
      !EnsureGpuOpaqueEmit(state))
  {
    return false;
  }

  std::vector<uint8_t> occ;
  BuildPaddedOccupancy(snapshot, registry, occ);
  std::vector<uint32_t> occ_words;
  PackBytes(occ.data(), occ.size(), occ_words);

  std::array<uint8_t, CHUNK_VOLUME> blocks{};
  std::array<uint8_t, CHUNK_VOLUME> lights{};
  for (int i = 0; i < CHUNK_VOLUME; ++i)
  {
    blocks[static_cast<size_t>(i)] =
        static_cast<uint8_t>(snapshot.blocks[static_cast<size_t>(i)]);
    lights[static_cast<size_t>(i)] = snapshot.GetLightPackedLocal(
        glm::ivec3(i % CHUNK_SIZE, (i / CHUNK_SIZE) % CHUNK_SIZE,
                   i / (CHUNK_SIZE * CHUNK_SIZE)));
  }
  std::vector<uint32_t> block_words;
  std::vector<uint32_t> light_words;
  PackBytes(blocks.data(), blocks.size(), block_words);
  PackBytes(lights.data(), lights.size(), light_words);

  const uint32_t volume = static_cast<uint32_t>(CHUNK_VOLUME);
  const uint32_t side = static_cast<uint32_t>(CHUNK_SIZE);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.OccSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(occ_words.size() * sizeof(uint32_t)),
               occ_words.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.MaskSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(volume * sizeof(uint32_t)), nullptr,
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.BlocksSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(block_words.size() * sizeof(uint32_t)),
               block_words.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.LightsSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(light_words.size() * sizeof(uint32_t)),
               light_words.data(), GL_DYNAMIC_DRAW);

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, state.OccSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, state.MaskSsbo);
  glUseProgram(state.MaskProgram);
  glUniform1ui(glGetUniformLocation(state.MaskProgram, "volume"), volume);
  glUniform1ui(glGetUniformLocation(state.MaskProgram, "side"), side);
  glUniform1ui(glGetUniformLocation(state.MaskProgram, "pad"),
               static_cast<uint32_t>(kGpuOccPad));
  glDispatchCompute((volume + 63u) / 64u, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  const std::array<uint32_t, 4> zero_counters{0, 0, 0, 0};
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.CountersSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(zero_counters),
               zero_counters.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, state.MaskSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, state.BlocksSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, state.LightsSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, state.RectsSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, state.CountersSsbo);
  glUseProgram(state.GreedyProgram);
  glUniform1ui(glGetUniformLocation(state.GreedyProgram, "side"), side);
  glDispatchCompute(kGpuPlaneWorkgroups, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  std::array<uint32_t, 4> counters{};
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.CountersSsbo);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(counters),
                     counters.data());
  const uint32_t rect_count = counters[0];
  if (rect_count == 0)
  {
    glUseProgram(0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return false;
  }
  if (rect_count > kMaxGpuRects)
  {
    LOG(WARNING) << "[GpuGreedyOpaqueEmit] rect overflow " << rect_count;
    glUseProgram(0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return false;
  }

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, state.RectsSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, state.VertSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, state.IndexSsbo);
  glUseProgram(state.EmitProgram);
  glUniform1ui(glGetUniformLocation(state.EmitProgram, "numRects"), rect_count);
  glUniform3i(glGetUniformLocation(state.EmitProgram, "chunkCoord"), coord.x,
              coord.y, coord.z);
  glUniform1ui(glGetUniformLocation(state.EmitProgram, "side"), side);
  glDispatchCompute((rect_count + 63u) / 64u, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  glUseProgram(0);

  struct GpuRectCpu
  {
    uint32_t axis, faceSign, slice, u, v, width, height, blockId, lightPacked;
  };
  std::vector<GpuRectCpu> rects(rect_count);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.RectsSsbo);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                     static_cast<GLsizeiptr>(rects.size() * sizeof(GpuRectCpu)),
                     rects.data());

  const size_t vert_count = static_cast<size_t>(rect_count) * 4;
  const size_t index_count = static_cast<size_t>(rect_count) * 6;
  std::vector<float> vert_data(vert_count * kGpuVertFloats);
  std::vector<uint32_t> index_data(index_count);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.VertSsbo);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                     static_cast<GLsizeiptr>(vert_data.size() * sizeof(float)),
                     vert_data.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.IndexSsbo);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                     static_cast<GLsizeiptr>(index_data.size() *
                                             sizeof(uint32_t)),
                     index_data.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  std::unordered_map<BlockId, GreedyMeshBatch> byBlockId;
  for (uint32_t r = 0; r < rect_count; ++r)
  {
    const BlockId id = static_cast<BlockId>(rects[r].blockId);
    GreedyMeshBatch &batch = byBlockId[id];
    batch.blockId = id;
    batch.Transparent = registry.IsTransparent(id);
    batch.AlphaCutout =
        registry.GetRenderStyle(id) == BlockRenderStyle::Cutout;
    const size_t base_vertex = batch.vertices.size();
    for (size_t v = 0; v < 4; ++v)
    {
      const size_t o = (static_cast<size_t>(r) * 4 + v) * kGpuVertFloats;
      GreedyMeshVertex vtx;
      vtx.px = vert_data[o + 0];
      vtx.py = vert_data[o + 1];
      vtx.pz = vert_data[o + 2];
      vtx.faceIndex = vert_data[o + 3];
      vtx.u = vert_data[o + 4];
      vtx.v = vert_data[o + 5];
      vtx.skyLight = vert_data[o + 6];
      vtx.blockLight = vert_data[o + 7];
      vtx.wetness = vert_data[o + 8];
      batch.vertices.push_back(vtx);
    }
    for (size_t i = 0; i < 6; ++i)
    {
      batch.indices.push_back(static_cast<uint32_t>(base_vertex) +
                              (index_data[static_cast<size_t>(r) * 6 + i] -
                               static_cast<uint32_t>(r) * 4));
    }
  }

  out_batches.reserve(byBlockId.size());
  for (auto &entry : byBlockId)
  {
    entry.second.blockId = entry.first;
    out_batches.push_back(std::move(entry.second));
  }
  ++gOpaqueEmitGpu;
  return !out_batches.empty();
#endif
}

uint64_t ConsumeGpuOpaqueEmitCount()
{
  const uint64_t v = gOpaqueEmitGpu;
  gOpaqueEmitGpu = 0;
  return v;
}

} // namespace cutum
