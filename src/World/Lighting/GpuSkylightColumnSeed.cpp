#include "World/Lighting/GpuSkylightColumnSeed.h"
#include "Blocks/BlockRegistry.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"
#include <vector>

#if defined(_WIN32) && !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
#include <windows.h>
#endif

namespace cutum
{
namespace
{

uint64_t gSkylightDispatches = 0;

#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
const char *kSkylightSeedCompute = R"(#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Occ { uint occ[]; };
layout(std430, binding = 1) writeonly buffer Sky { uint sky[]; };
uniform uint side;

uint occAt(int x, int y, int z) {
  uint i = uint((y * int(side) + z) * int(side) + x);
  uint word = occ[i >> 2];
  return (word >> ((i & 3u) * 8u)) & 0xffu;
}

void main() {
  uint col = gl_GlobalInvocationID.x;
  uint n2 = side * side;
  if (col >= n2) return;
  int x = int(col % side);
  int z = int(col / side);
  uint level = 15u;
  for (int y = int(side) - 1; y >= 0; --y) {
    uint i = uint((y * int(side) + z) * int(side) + x);
    if (occAt(x, y, z) != 0u) {
      sky[i] = 0u;
      level = 0u;
    } else {
      sky[i] = level;
    }
  }
}
)";

struct GpuSeedState
{
  GLuint Program{0};
  GLuint OccSsbo{0};
  GLuint SkySsbo{0};
  bool InitAttempted{false};
};

GpuSeedState &SeedState()
{
  static GpuSeedState s;
  return s;
}

bool EnsureSeedCompute()
{
  auto &s = SeedState();
  if (s.InitAttempted)
  {
    return s.Program != 0;
  }
  s.InitAttempted = true;
  const GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &kSkylightSeedCompute, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[512];
    glGetShaderInfoLog(sh, 512, nullptr, log);
    LOG(WARNING) << "[GpuSkylight] compile failed: " << log;
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
  glGenBuffers(1, &s.OccSsbo);
  glGenBuffers(1, &s.SkySsbo);
  return true;
}
#endif

} // namespace

uint64_t GpuSkylightSeedDispatchCount() { return gSkylightDispatches; }

bool TryGpuSeedSkylightColumns(const std::array<uint8_t, CHUNK_VOLUME> &occ,
                               std::array<uint8_t, CHUNK_VOLUME> &sky_out)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)occ;
  (void)sky_out;
  return false;
#else
  if (!EnsureSeedCompute())
  {
    return false;
  }
  auto &s = SeedState();
  std::vector<uint32_t> occ_words((CHUNK_VOLUME + 3) / 4, 0);
  for (int i = 0; i < CHUNK_VOLUME; ++i)
  {
    occ_words[static_cast<size_t>(i >> 2)] |=
        static_cast<uint32_t>(occ[static_cast<size_t>(i)]) << ((i & 3) * 8);
  }
  const uint32_t side = static_cast<uint32_t>(CHUNK_SIZE);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.OccSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(occ_words.size() * sizeof(uint32_t)),
               occ_words.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.SkySsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(CHUNK_VOLUME * sizeof(uint32_t)), nullptr,
               GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s.OccSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s.SkySsbo);
  glUseProgram(s.Program);
  glUniform1ui(glGetUniformLocation(s.Program, "side"), side);
  const uint32_t cols = side * side;
  glDispatchCompute((cols + 63u) / 64u, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
  glUseProgram(0);
  std::vector<uint32_t> sky(CHUNK_VOLUME, 0);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.SkySsbo);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                     static_cast<GLsizeiptr>(sky.size() * sizeof(uint32_t)),
                     sky.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  for (int i = 0; i < CHUNK_VOLUME; ++i)
  {
    sky_out[static_cast<size_t>(i)] =
        static_cast<uint8_t>(sky[static_cast<size_t>(i)] & 0xffu);
  }
  ++gSkylightDispatches;
  return true;
#endif
}

namespace
{
uint64_t gSkylightApplies = 0;
}

uint64_t ConsumeGpuSkylightSeedApplyCount()
{
  const uint64_t v = gSkylightApplies;
  gSkylightApplies = 0;
  return v;
}

bool ApplyGpuSkylightSeedToChunk(UChunk &chunk, UBlockRegistry &registry)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)chunk;
  (void)registry;
  return false;
#else
#if defined(_WIN32)
  if (wglGetCurrentContext() == nullptr)
  {
    return false;
  }
#endif
  std::array<uint8_t, CHUNK_VOLUME> occ{};
  for (int y = 0; y < CHUNK_SIZE; ++y)
  {
    for (int z = 0; z < CHUNK_SIZE; ++z)
    {
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const glm::ivec3 local(x, y, z);
        const BlockId id = chunk.GetBlockLocal(local);
        const int li = (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
        occ[static_cast<size_t>(li)] =
            (id != 0 && !registry.IsTransparent(id) && registry.IsSolid(id))
                ? 1u
                : 0u;
      }
    }
  }
  std::array<uint8_t, CHUNK_VOLUME> sky{};
  if (!TryGpuSeedSkylightColumns(occ, sky))
  {
    return false;
  }
  for (int y = 0; y < CHUNK_SIZE; ++y)
  {
    for (int z = 0; z < CHUNK_SIZE; ++z)
    {
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const glm::ivec3 local(x, y, z);
        const int li = (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
        const int block_level = chunk.GetBlockLightLocal(local);
        chunk.SetLightLocal(local, static_cast<int>(sky[static_cast<size_t>(li)]),
                            block_level);
      }
    }
  }
  ++gSkylightApplies;
  return true;
#endif
}

} // namespace cutum
