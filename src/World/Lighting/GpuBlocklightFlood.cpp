#include "World/Lighting/GpuBlocklightFlood.h"

#include "Blocks/BlockRegistry.h"
#include "Render/GlIncludes.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkLighting.h"
#include "World/Lighting/LightUtil.h"
#include "glog/logging.h"

#include <cstdlib>
#include <vector>

#if defined(_WIN32) && !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
#include <windows.h>
#endif

namespace cutum
{
namespace
{

uint64_t gBlocklightFloods = 0;
uint64_t gBlocklightDispatches = 0;

#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
const char *kBlocklightFloodCompute = R"(#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer LightA { uint light_a[]; };
layout(std430, binding = 1) buffer LightB { uint light_b[]; };
layout(std430, binding = 2) readonly buffer Emit { uint emit[]; };
layout(std430, binding = 3) readonly buffer Trans { uint trans[]; };
uniform uint side;
uniform uint read_a;

uint idx(int x, int y, int z) {
  return uint((y * int(side) + z) * int(side) + x);
}

uint sampleLight(int x, int y, int z) {
  if (x < 0 || y < 0 || z < 0 || x >= int(side) || y >= int(side) || z >= int(side)) {
    return 0u;
  }
  const uint i = idx(x, y, z);
  return read_a != 0u ? light_a[i] : light_b[i];
}

void main() {
  uint i = gl_GlobalInvocationID.x;
  uint vol = side * side * side;
  if (i >= vol) return;
  int z = int(i % side);
  int y = int((i / side) % side);
  int x = int(i / (side * side));
  if (trans[i] == 0u) {
    if (read_a != 0u) {
      light_b[i] = 0u;
    } else {
      light_a[i] = 0u;
    }
    return;
  }
  uint best = emit[i];
  const int dx[6] = int[6](1, -1, 0, 0, 0, 0);
  const int dy[6] = int[6](0, 0, 1, -1, 0, 0);
  const int dz[6] = int[6](0, 0, 0, 0, 1, -1);
  for (int n = 0; n < 6; ++n) {
    uint nl = sampleLight(x + dx[n], y + dy[n], z + dz[n]);
    if (nl > 1u) {
      best = max(best, nl - 1u);
    }
  }
  if (read_a != 0u) {
    light_b[i] = best;
  } else {
    light_a[i] = best;
  }
}
)";

struct BlocklightFloodState
{
  GLuint Program{0};
  GLuint LightA{0};
  GLuint LightB{0};
  GLuint EmitSsbo{0};
  GLuint TransSsbo{0};
  bool InitAttempted{false};
};

BlocklightFloodState &BlocklightFloodStateInstance()
{
  static BlocklightFloodState s;
  return s;
}

bool EnsureFloodCompute()
{
  auto &s = BlocklightFloodStateInstance();
  if (s.InitAttempted)
  {
    return s.Program != 0;
  }
  s.InitAttempted = true;
  const GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &kBlocklightFloodCompute, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[512];
    glGetShaderInfoLog(sh, 512, nullptr, log);
    LOG(WARNING) << "[GpuBlocklight] compile failed: " << log;
    glDeleteShader(sh);
    return false;
  }
  s.Program = glCreateProgram();
  glAttachShader(s.Program, sh);
  glLinkProgram(s.Program);
  glDeleteShader(sh);
  glGetProgramiv(s.Program, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    glDeleteProgram(s.Program);
    s.Program = 0;
    return false;
  }
  glGenBuffers(1, &s.LightA);
  glGenBuffers(1, &s.LightB);
  glGenBuffers(1, &s.EmitSsbo);
  glGenBuffers(1, &s.TransSsbo);
  return true;
}

bool GpuBlocklightComputeEnabled()
{
  const char *env = std::getenv("CUBATARIUM_GPU_BLOCKLIGHT_COMPUTE");
  return env && env[0] == '1';
}

void DispatchGpuBlocklightFloodNoReadback(const UChunk &chunk,
                                          const UBlockRegistry &registry)
{
  if (!GpuBlocklightComputeEnabled() || !EnsureFloodCompute())
  {
    return;
  }
  auto &s = BlocklightFloodStateInstance();
  std::vector<uint32_t> emit(CHUNK_VOLUME, 0);
  std::vector<uint32_t> trans(CHUNK_VOLUME, 0);
  std::vector<uint32_t> light(CHUNK_VOLUME, 0);
  for (int ly = 0; ly < CHUNK_SIZE; ++ly)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        const glm::ivec3 local(lx, ly, lz);
        const int li = (ly * CHUNK_SIZE + lz) * CHUNK_SIZE + lx;
        const BlockId id = chunk.GetBlockLocal(local);
        trans[static_cast<size_t>(li)] =
            IsLightTransparent(registry, id) ? 1u : 0u;
        emit[static_cast<size_t>(li)] =
            static_cast<uint32_t>(registry.GetLightEmission(id));
        light[static_cast<size_t>(li)] =
            static_cast<uint32_t>(chunk.GetBlockLightLocal(local));
      }
    }
  }
  const uint32_t side = static_cast<uint32_t>(CHUNK_SIZE);
  const GLsizeiptr bytes =
      static_cast<GLsizeiptr>(CHUNK_VOLUME * sizeof(uint32_t));
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.LightA);
  glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, light.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.LightB);
  glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.EmitSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, emit.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.TransSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, trans.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, s.EmitSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, s.TransSsbo);
  glUseProgram(s.Program);
  glUniform1ui(glGetUniformLocation(s.Program, "side"), side);
  const uint32_t groups = (static_cast<uint32_t>(CHUNK_VOLUME) + 63u) / 64u;
  for (int pass = 0; pass < 15; ++pass)
  {
    const uint32_t read_a = (pass & 1) == 0 ? 1u : 0u;
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s.LightA);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s.LightB);
    glUniform1ui(glGetUniformLocation(s.Program, "read_a"), read_a);
    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  }
  glUseProgram(0);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  ++gBlocklightDispatches;
}
#endif

} // namespace

void NoteGpuBlocklightFlood() { ++gBlocklightFloods; }

uint64_t ConsumeGpuBlocklightFloodCount()
{
  const uint64_t v = gBlocklightFloods;
  gBlocklightFloods = 0;
  return v;
}

bool TryGpuPropagateBlocklight(UBlockWorld &world, UBlockRegistry &registry,
                               glm::ivec3 chunk_coord)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)world;
  (void)registry;
  (void)chunk_coord;
  return false;
#else
#if defined(_WIN32)
  if (wglGetCurrentContext() == nullptr)
  {
    return false;
  }
#endif
  // GPF4: authoritative CPU flood (no sync full-volume readback).
  PropagateBlocklight(world, registry, chunk_coord);
  if (const UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord))
  {
    DispatchGpuBlocklightFloodNoReadback(*chunk, registry);
  }
  return true;
#endif
}

} // namespace cutum
