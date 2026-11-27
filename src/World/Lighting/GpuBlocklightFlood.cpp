#include "World/Lighting/GpuBlocklightFlood.h"

#include "Blocks/BlockRegistry.h"
#include "Render/Gl/ComputeBufferBindings.h"
#include "Render/Gl/ComputeScratchBuffers.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkLighting.h"
#include "World/Lighting/LightUtil.h"

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

ComputeScratchBuffers<4> &BlocklightFloodStateInstance()
{
  static thread_local ComputeScratchBuffers<4> s;
  return s;
}

bool GpuBlocklightComputeEnabled()
{
  const char *env = std::getenv("CUBATARIUM_GPU_BLOCKLIGHT_COMPUTE");
  return env && env[0] == '1';
}

void DispatchGpuBlocklightFloodNoReadback(const UChunk &chunk,
                                          const UBlockRegistry &registry)
{
  if (!GpuBlocklightComputeEnabled() || !ComputeStorageAvailable())
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
  if (!s.Initialize([](UComputeProgram &program) { return program.CompileSource(kBlocklightFloodCompute); }))
    return;
  const ComputeBufferBindings<4> restore;
  const std::array<const uint32_t *, 4> sources{light.data(), nullptr, emit.data(), trans.data()};
  for (GLuint i = 0; i < s.names.size(); ++i)
  {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.names[i]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, sources[i], GL_DYNAMIC_DRAW);
  }
  if (glGetError() != GL_NO_ERROR)
  {
    s.DestroyBuffers();
    return;
  }
  for (GLuint i = 0; i < s.names.size(); ++i)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, s.names[i]);
  glUseProgram(s.program);
  glUniform1ui(glGetUniformLocation(s.program, "side"), side);
  const auto readALocation = glGetUniformLocation(s.program, "read_a");
  const uint32_t groups = (static_cast<uint32_t>(CHUNK_VOLUME) + 63u) / 64u;
  for (int pass = 0; pass < 15; ++pass)
  {
    const uint32_t read_a = (pass & 1) == 0 ? 1u : 0u;
    glUniform1ui(readALocation, read_a);
    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  }
  if (glGetError() == GL_NO_ERROR)
    ++gBlocklightDispatches;
}
#endif

} // namespace

void InspectBlocklightFloodGpuBuffers(TerrainGpuBufferCensus &out)
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  BlocklightFloodStateInstance().Inspect(out, TerrainGpuBufferRole::BlocklightFlood);
#endif
}

void ReleaseBlocklightFloodGpuStorage()
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  BlocklightFloodStateInstance().Release(CurrentComputeContext());
#endif
}

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
