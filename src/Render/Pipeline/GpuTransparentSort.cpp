#include "Render/Pipeline/GpuTransparentSort.h"
#include "Render/Pipeline/GreedyTransparentSort.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"
#include <algorithm>
#include <cmath>
#include <cstring>
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

uint64_t gTransparentSortGpu = 0;

constexpr uint32_t kMaxGpuSortKeys = 4096u;
constexpr size_t kMinGpuSortRefs = 4u;

#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)

struct GpuSortKey
{
  float dist;
  uint32_t layer;
  uint32_t blockId;
  uint32_t index;
};

const char *kBitonicSortCompute = R"(#version 430
layout(local_size_x = 256) in;
struct SortKey { float dist; uint layer; uint blockId; uint index; };
layout(std430, binding = 0) buffer Keys { SortKey keys[]; };
uniform uint n;
uniform uint blockHeight;
uniform uint blockStep;

bool keyLess(SortKey a, SortKey b) {
  if (abs(a.dist - b.dist) > 0.25) return a.dist > b.dist;
  if (a.layer != b.layer) return a.layer < b.layer;
  return a.blockId < b.blockId;
}

void main() {
  uint i = gl_GlobalInvocationID.x;
  uint j = i ^ blockStep;
  if (j <= i || i >= n || j >= n) return;
  bool ascending = ((i & blockHeight) == 0u);
  SortKey ki = keys[i];
  SortKey kj = keys[j];
  bool lessIJ = keyLess(ki, kj);
  bool swap = ascending ? !lessIJ : lessIJ;
  if (swap) {
    keys[i] = kj;
    keys[j] = ki;
  }
}
)";

struct GpuSortState
{
  GLuint Program{0};
  GLuint KeysSsbo{0};
  bool InitAttempted{false};
};

GpuSortState &SortState()
{
  static GpuSortState state;
  return state;
}

GLuint CompileSortProgram()
{
  const GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &kBitonicSortCompute, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[512];
    glGetShaderInfoLog(sh, 512, nullptr, log);
    LOG(WARNING) << "[GpuTransparentSort] compile failed: " << log;
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
    LOG(WARNING) << "[GpuTransparentSort] link failed";
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

bool EnsureSortProgram()
{
  GpuSortState &state = SortState();
  if (state.InitAttempted)
  {
    return state.Program != 0;
  }
  state.InitAttempted = true;
  state.Program = CompileSortProgram();
  if (!state.Program)
  {
    return false;
  }
  glGenBuffers(1, &state.KeysSsbo);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.KeysSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(kMaxGpuSortKeys * sizeof(GpuSortKey)),
               nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  return true;
}

uint32_t NextPow2(uint32_t v)
{
  if (v <= 1)
  {
    return 1;
  }
  --v;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  return v + 1;
}

void BuildHostKeys(const std::vector<GreedyBatchRef> &refs,
                   const UChunkMeshCache &cache, const glm::vec3 &cameraPos,
                   const UBlockRegistry &registry, std::vector<GpuSortKey> &keys)
{
  keys.resize(refs.size());
  for (size_t i = 0; i < refs.size(); ++i)
  {
    const GreedyMeshBatch *batch = cache.TryGetGreedyBatch(refs[i]);
    if (!batch)
    {
      keys[i] = GpuSortKey{-1.0f, 99u, 0u, static_cast<uint32_t>(i)};
      continue;
    }
    keys[i].dist = GreedyBatchViewDistance(*batch, cameraPos);
    keys[i].layer = static_cast<uint32_t>(
        TransparentBatchLayer(registry.GetRenderStyle(batch->blockId)));
    keys[i].blockId = static_cast<uint32_t>(batch->blockId);
    keys[i].index = static_cast<uint32_t>(i);
  }
}

#endif

} // namespace

bool TryGpuSortTransparentGreedyBatches(
    std::vector<GreedyBatchRef> &refs, const UChunkMeshCache &cache,
    const glm::vec3 &cameraPos, const UBlockRegistry &registry)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)refs;
  (void)cache;
  (void)cameraPos;
  (void)registry;
  return false;
#else
#if defined(_WIN32)
  if (wglGetCurrentContext() == nullptr)
  {
    return false;
  }
#endif
  if (refs.size() < kMinGpuSortRefs || !EnsureSortProgram())
  {
    return false;
  }
  if (refs.size() > kMaxGpuSortKeys)
  {
    return false;
  }

  GpuSortState &state = SortState();
  std::vector<GpuSortKey> keys;
  BuildHostKeys(refs, cache, cameraPos, registry, keys);

  const uint32_t count = static_cast<uint32_t>(keys.size());
  const uint32_t n = NextPow2(count);
  keys.resize(n);
  for (uint32_t i = count; i < n; ++i)
  {
    keys[i] = GpuSortKey{-1e9f, 0u, 0u, UINT32_MAX};
  }

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.KeysSsbo);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                  static_cast<GLsizeiptr>(keys.size() * sizeof(GpuSortKey)),
                  keys.data());
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, state.KeysSsbo);
  glUseProgram(state.Program);
  const GLint loc_n = glGetUniformLocation(state.Program, "n");
  const GLint loc_h = glGetUniformLocation(state.Program, "blockHeight");
  const GLint loc_s = glGetUniformLocation(state.Program, "blockStep");
  glUniform1ui(loc_n, n);
  for (uint32_t k = 2; k <= n; k <<= 1)
  {
    for (uint32_t j = k >> 1; j > 0; j >>= 1)
    {
      glUniform1ui(loc_h, k);
      glUniform1ui(loc_s, j);
      glDispatchCompute((n + 255u) / 256u, 1, 1);
      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
  }
  glUseProgram(0);

  keys.resize(count);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                     static_cast<GLsizeiptr>(keys.size() * sizeof(GpuSortKey)),
                     keys.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  std::vector<GreedyBatchRef> ordered;
  ordered.reserve(refs.size());
  for (const GpuSortKey &key : keys)
  {
    if (key.index < refs.size())
    {
      ordered.push_back(refs[key.index]);
    }
  }
  if (ordered.size() != refs.size())
  {
    return false;
  }
  refs = std::move(ordered);
  ++gTransparentSortGpu;
  return true;
#endif
}

uint64_t ConsumeGpuTransparentSortCount()
{
  const uint64_t v = gTransparentSortGpu;
  gTransparentSortGpu = 0;
  return v;
}

} // namespace cutum
