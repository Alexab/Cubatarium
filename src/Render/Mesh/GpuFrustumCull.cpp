#include "Render/Mesh/GpuFrustumCull.h"
#include "Render/Camera/Frustum.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"
#include <algorithm>
#include <vector>

namespace cutum
{
namespace
{

// Matches Frustum::IntersectsChunkAABB plane skips (near/top/bottom = 2,3,4).
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
  vec4 camPosMaxDist; // xyz = camera, w = maxDist (0 = unlimited)
  uint count;
  uint horizontalDist;
  uint _pad1;
  uint _pad2;
};

bool sphereInFrustum(vec3 c, float r) {
  for (int i = 0; i < 6; ++i) {
    if (i == 2 || i == 3 || i == 4) continue;
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
  vec3 c = a.xyz;
  float r = a.w;
  float maxDist = camPosMaxDist.w;
  bool inDist = true;
  if (maxDist > 0.0) {
    vec3 cam = camPosMaxDist.xyz;
    float d = (horizontalDist != 0u)
                  ? length(vec2(c.x - cam.x, c.z - cam.z))
                  : length(c - cam);
    // Keep near-camera spheres (mirrors IntersectsChunkAABB distance admit).
    inDist = d <= maxDist + r;
  }
  vis[i] = (sphereInFrustum(c, r) && inDist) ? 1u : 0u;
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
  // Sync SSBO readback is only a win for modest sphere counts. Large rings
  // stay on CPU Delegate (same flat-ref rebuild) so wall/gpu_cull_ms stay sane.
  constexpr uint32_t kMaxGpuCullSpheres = 384;

  if (!frustum || !camera_pos || !EnsureGpu())
  {
    Delegate.RebuildVisible(cache, frustum, camera_pos, max_cull_distance);
    return;
  }

  std::vector<UChunkMeshCache::CullSphereEntry> entries;
  cache.CollectGreedyCullSpheres(entries);
  if (entries.empty() || entries.size() > kMaxGpuCullSpheres)
  {
    Delegate.RebuildVisible(cache, frustum, camera_pos, max_cull_distance);
    return;
  }

  const uint32_t count = static_cast<uint32_t>(entries.size());
  std::vector<glm::vec4> aabbs(count);
  for (uint32_t i = 0; i < count; ++i)
  {
    aabbs[i] = entries[i].sphere;
  }

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, State->AabbSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(aabbs.size() * sizeof(glm::vec4)),
               aabbs.data(), GL_DYNAMIC_DRAW);
  std::vector<uint32_t> vis(count, 0);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, State->VisSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(vis.size() * sizeof(uint32_t)),
               nullptr, GL_DYNAMIC_DRAW);

  struct Ubo
  {
    glm::vec4 planes[6];
    glm::vec4 camPosMaxDist;
    uint32_t count;
    uint32_t horizontalDist;
    uint32_t pad[2];
  } ubo{};
  for (int i = 0; i < 6; ++i)
  {
    ubo.planes[i] = frustum->planes[static_cast<size_t>(i)];
  }
  ubo.camPosMaxDist =
      glm::vec4(*camera_pos, std::max(0.0f, max_cull_distance));
  ubo.count = count;
  ubo.horizontalDist =
      (cache.GetAltitudeAboveTerrain() >
       static_cast<float>(cache.GetAltitudeFogThresholdBlocks()))
          ? 1u
          : 0u;

  glBindBuffer(GL_UNIFORM_BUFFER, State->FrustumUbo);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(ubo), &ubo, GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, State->AabbSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, State->VisSsbo);
  glBindBufferBase(GL_UNIFORM_BUFFER, 2, State->FrustumUbo);
  glUseProgram(State->Program);
  glDispatchCompute((count + 63u) / 64u, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
  glUseProgram(0);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, State->VisSsbo);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                     static_cast<GLsizeiptr>(vis.size() * sizeof(uint32_t)),
                     vis.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  cache.RebuildFlatGreedyFromVisibilityMask(vis.data(), vis.size(), entries);
}

} // namespace cutum
