#include "Render/Pipeline/GpuTransparentSort.h"
#include "Render/Pipeline/GreedyTransparentSort.h"
#include "Render/Gl/ComputeBufferBindings.h"
#include "Render/Gl/ComputeScratchBuffers.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace cutum
{
namespace
{

uint64_t gTransparentSortGpu = 0;
uint64_t gTransparentSortReadback = 0;

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

ComputeScratchBuffers<1> &SortState()
{
  static thread_local ComputeScratchBuffers<1> state;
  return state;
}

bool EnsureSortProgram()
{
  auto &state = SortState();
  const bool warm = state.names.front() != 0;
  if (!state.Initialize([](UComputeProgram &p) { return p.CompileSource(kBitonicSortCompute); }))
    return false;
  if (warm)
    return true;
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.names.front());
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(kMaxGpuSortKeys * sizeof(GpuSortKey)),
               nullptr, GL_DYNAMIC_DRAW);
  if (glGetError() == GL_NO_ERROR)
    return true;
  state.DestroyBuffers();
  return false;
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

void InspectTransparentSortGpuBuffers(TerrainGpuBufferCensus &out)
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  SortState().Inspect(out, TerrainGpuBufferRole::TransparentSort);
#endif
}

void ReleaseTransparentSortGpuStorage()
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  SortState().Release(CurrentComputeContext());
#endif
}

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
  if (refs.size() < kMinGpuSortRefs || refs.size() > kMaxGpuSortKeys || !ComputeStorageAvailable())
    return false;

  std::vector<GpuSortKey> keys;
  BuildHostKeys(refs, cache, cameraPos, registry, keys);

  const uint32_t count = static_cast<uint32_t>(keys.size());
  const uint32_t n = NextPow2(count);
  keys.resize(n);
  for (uint32_t i = count; i < n; ++i)
  {
    keys[i] = GpuSortKey{-1e9f, 0u, 0u, UINT32_MAX};
  }

  const ComputeBufferBindings<1> restore;
  if (!EnsureSortProgram())
    return false;
  const auto &state = SortState();
  const GLuint keysSsbo = state.names.front();
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, keysSsbo);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                  static_cast<GLsizeiptr>(keys.size() * sizeof(GpuSortKey)),
                  keys.data());
  if (glGetError() != GL_NO_ERROR)
    return false;
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, keysSsbo);
  glUseProgram(state.program);
  const GLint loc_n = glGetUniformLocation(state.program, "n");
  const GLint loc_h = glGetUniformLocation(state.program, "blockHeight");
  const GLint loc_s = glGetUniformLocation(state.program, "blockStep");
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
  glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
  if (glGetError() != GL_NO_ERROR)
    return false;

  keys.resize(count);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                     static_cast<GLsizeiptr>(keys.size() * sizeof(GpuSortKey)),
                     keys.data());
  if (glGetError() != GL_NO_ERROR)
    return false;
  NoteGpuTransparentSortReadback();

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

void NoteGpuTransparentSortReadback() { ++gTransparentSortReadback; }

uint64_t ConsumeGpuTransparentSortReadbackCount()
{
  const uint64_t v = gTransparentSortReadback;
  gTransparentSortReadback = 0;
  return v;
}

} // namespace cutum
