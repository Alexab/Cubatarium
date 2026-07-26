#include "Render/Mesh/GpuFrustumCull.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace cutum
{
namespace
{

const char *kFrustumCullComputeSrc = R"(#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer InAabb {
  vec4 aabb[]; // xyz = center, w = radius
};
layout(std430, binding = 1) writeonly buffer OutVis {
  uint vis[];
};
layout(std140, binding = 2) uniform FrustumUBO {
  vec4 planes[6];
  uint count;
  uint _pad0;
  uint _pad1;
  uint _pad2;
};

bool sphereInFrustum(vec3 c, float r) {
  for (int i = 0; i < 6; ++i) {
    if (dot(planes[i].xyz, c) + planes[i].w < -r) {
      return false;
    }
  }
  return true;
}

void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= count) {
    return;
  }
  vec4 a = aabb[i];
  vis[i] = sphereInFrustum(a.xyz, a.w) ? 1u : 0u;
}
)";

GLuint CompileCompute(const char *src)
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
    LOG(WARNING) << "[GpuFrustumCull] compute compile failed: " << log;
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
    char log[512];
    glGetProgramInfoLog(prog, 512, nullptr, log);
    LOG(WARNING) << "[GpuFrustumCull] compute link failed: " << log;
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

} // namespace

struct UGpuFrustumCull::GpuState
{
  GLuint Program{0};
  GLuint AabbSsbo{0};
  GLuint VisSsbo{0};
  GLuint FrustumUbo{0};
  bool InitAttempted{false};
};

UGpuFrustumCull::UGpuFrustumCull() : State(std::make_unique<GpuState>()) {}

UGpuFrustumCull::~UGpuFrustumCull()
{
  if (!State)
  {
    return;
  }
  if (State->Program)
  {
    glDeleteProgram(State->Program);
  }
  if (State->AabbSsbo)
  {
    glDeleteBuffers(1, &State->AabbSsbo);
  }
  if (State->VisSsbo)
  {
    glDeleteBuffers(1, &State->VisSsbo);
  }
  if (State->FrustumUbo)
  {
    glDeleteBuffers(1, &State->FrustumUbo);
  }
}

bool UGpuFrustumCull::EnsureGpu()
{
  if (State->InitAttempted)
  {
    return State->Program != 0;
  }
  State->InitAttempted = true;
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  return false;
#else
  State->Program = CompileCompute(kFrustumCullComputeSrc);
  if (!State->Program)
  {
    return false;
  }
  glGenBuffers(1, &State->AabbSsbo);
  glGenBuffers(1, &State->VisSsbo);
  glGenBuffers(1, &State->FrustumUbo);
  return true;
#endif
}

void UGpuFrustumCull::RebuildVisible(UChunkMeshCache &cache,
                                     const Frustum *frustum,
                                     const glm::vec3 *camera_pos,
                                     float max_cull_distance)
{
  // Warm compute path once (Desktop). Per-frame AABB compaction of the full
  // greedy set is a follow-up; visible lists still come from CPU rebuild so
  // streaming wall stays near cb_pack.
  if (!Warmed && EnsureGpu())
  {
    Warmed = true;
    const uint32_t count = 64;
    std::vector<glm::vec4> aabbs(count, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    if (camera_pos)
    {
      aabbs[0] = glm::vec4(camera_pos->x, camera_pos->y, camera_pos->z, 8.0f);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, State->AabbSsbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, aabbs.size() * sizeof(glm::vec4),
                 aabbs.data(), GL_DYNAMIC_DRAW);
    std::vector<uint32_t> vis(count, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, State->VisSsbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vis.size() * sizeof(uint32_t),
                 vis.data(), GL_DYNAMIC_DRAW);
    struct Ubo
    {
      glm::vec4 planes[6];
      uint32_t count;
      uint32_t pad[3];
    } ubo{};
    for (int i = 0; i < 6; ++i)
    {
      ubo.planes[i] = glm::vec4(0.0f, 1.0f, 0.0f, 1.0e6f);
    }
    ubo.count = count;
    glBindBuffer(GL_UNIFORM_BUFFER, State->FrustumUbo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(ubo), &ubo, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, State->AabbSsbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, State->VisSsbo);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, State->FrustumUbo);
    glUseProgram(State->Program);
    glDispatchCompute((count + 63) / 64, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(0);
  }
  (void)frustum;
  Delegate.RebuildVisible(cache, frustum, camera_pos, max_cull_distance);
}

} // namespace cutum
