#include "Render/Engine/GreedyGpuBackend.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Engine/GreedyVertexPool.h"
#include "Render/Camera/Frustum.h"
#include "Render/Camera/GpuPassRefreshPolicy.h"
#include "Render/GlIncludes.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/RuntimeTuning.h"
#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <unordered_set>

namespace cutum
{

namespace
{

constexpr unsigned int kArrayBuffer = GL_ARRAY_BUFFER;
constexpr unsigned int kElementArrayBuffer = GL_ELEMENT_ARRAY_BUFFER;

GreedyGpuRefreshTelem *gRefreshTelem = nullptr;

void NoteUploadFull()
{
  if (gRefreshTelem)
  {
    ++gRefreshTelem->UploadFullN;
  }
}

void NoteCmdReorder()
{
  if (gRefreshTelem)
  {
    ++gRefreshTelem->CmdReorderN;
  }
}

void NoteOrderOnlyFail(TransparentOrderOnlyFailReason reason)
{
  if (gRefreshTelem)
  {
    gRefreshTelem->OrderOnlyFailReason = static_cast<int>(reason);
  }
}

void ApplyPoolBudget(UGreedyVertexPool &pool)
{
  const auto &tune = URuntimeTuning::Get();
  const size_t max_bytes =
      static_cast<size_t>(std::max(0, tune.GpuVertexPoolMaxMb)) * 1024ull *
      1024ull;
  pool.SetMaxCapacityBytes(max_bytes);
  const size_t reserve_bytes =
      static_cast<size_t>(std::max(0, tune.GpuVertexPoolReserveMb)) * 1024ull *
      1024ull;
  if (reserve_bytes > 0)
  {
    // Split reserve ~3:1 vertex:index as a coarse heuristic.
    const size_t v = (reserve_bytes * 3) / 4;
    const size_t i = reserve_bytes - v;
    pool.EnsureMinCapacity(v, i);
  }
}

} // namespace

void UGreedyGpuBackend::BindRefreshTelem(GreedyGpuRefreshTelem *telem)
{
  gRefreshTelem = telem;
}

void UGreedyGpuBackend::UploadBuffer(GLuint &buffer, size_t &capacity_bytes,
                                     unsigned int target, const void *data,
                                     size_t byte_size)
{
  if (byte_size == 0)
  {
    return;
  }
  if (buffer == 0)
  {
    glGenBuffers(1, &buffer);
  }
  glBindBuffer(target, buffer);
  if (byte_size > capacity_bytes)
  {
    glBufferData(target, static_cast<GLsizeiptr>(byte_size), nullptr,
                 GL_DYNAMIC_DRAW);
    capacity_bytes = byte_size;
    glBufferSubData(target, 0, static_cast<GLsizeiptr>(byte_size), data);
  }
  else
  {
    glBufferSubData(target, 0, static_cast<GLsizeiptr>(byte_size), data);
  }
}

void UGreedyGpuBackend::RefreshPass(GreedyGpuPassCache &cache,
                                    const std::vector<GreedyMeshBatch> &batches,
                                    uint64_t mesh_revision,
                                    uint64_t cull_revision,
                                    uint64_t sort_revision)
{
  if (mesh_revision == cache.meshRevision &&
      cull_revision == cache.cullRevision &&
      sort_revision == cache.sortRevision)
  {
    return;
  }

  ApplyPoolBudget(cache.VertexPool);
  std::vector<GreedyGpuBatch> staged;
  staged.reserve(batches.size());
  bool ok = true;
  for (const auto &batch : batches)
  {
    if (batch.vertices.empty() || batch.indices.empty()) continue;
    GreedyGpuBatch gpu;
    UploadBatch(gpu, batch, cache.VertexPool);
    if (!gpu.pooled) { ok = false; break; }
    staged.push_back(gpu);
  }
  if (ok)
  {
    for (auto &gpu : cache.batches) ReleasePooledBatch(gpu, cache.VertexPool);
    cache.batches = std::move(staged);
    cache.meshRevision = mesh_revision;
    cache.cullRevision = cull_revision;
    cache.sortRevision = sort_revision;
    cache.IndirectCullReady = false;
    cache.GpuCompactActive = false;
  }
  else
    for (auto &gpu : staged) ReleasePooledBatch(gpu, cache.VertexPool);
  cache.poolVbo = cache.VertexPool.VertexBuffer();
  cache.poolEbo = cache.VertexPool.IndexBuffer();
  cache.usesVertexPool = !cache.batches.empty();
  for (auto &gpu : cache.batches)
    if (gpu.pooled) { gpu.vbo = cache.poolVbo; gpu.ebo = cache.poolEbo; }
}

void UGreedyGpuBackend::RefreshPassRefs(
    GreedyGpuPassCache &cache, const UChunkMeshCache &meshCache,
    const std::vector<GreedyBatchRef> &refs,
    uint64_t mesh_revision, uint64_t cull_revision, uint64_t sort_revision,
    bool consume_dirty)
{
  std::unordered_set<glm::ivec3, IVec3Hash> dirty;
  if (consume_dirty) meshCache.ConsumeGeometryDirtyChunks(dirty);
  else
    for (const auto &ref : refs)
      if (meshCache.GpuPassDirtyHitsChunk(ref.chunkCoord))
        dirty.insert(ref.chunkCoord);
  std::vector<GreedyGpuUploadInput> inputs;
  inputs.reserve(refs.size());
  for (const auto &ref : refs)
    inputs.push_back({ref, meshCache.TryGetGreedyBatch(ref)});
  ApplyPoolBudget(cache.VertexPool);
  const auto previous = cache.publicationVersion;
  if (!PublishPassInputs(cache, inputs, dirty, mesh_revision, cull_revision,
                         sort_revision))
    NoteOrderOnlyFail(TransparentOrderOnlyFailReason::PoolNotOk);
  else
  {
    if (previous != cache.publicationVersion) NoteCmdReorder();
    NoteOrderOnlyFail(TransparentOrderOnlyFailReason::Ok);
  }
  if (mesh_revision > cache.meshRevision)
    NotePassMeshRevLag(mesh_revision - cache.meshRevision);
}

void UGreedyGpuBackend::DestroyPass(GreedyGpuPassCache &cache)
{
  for (GreedyGpuBatch &batch : cache.batches)
  {
    DestroyBatchBuffers(batch);
  }
  cache.batches.clear();
  cache.PendingGeometryDirty.clear();
  cache.publicationVersion = 0;
  cache.meshRevision = 0;
  cache.cullRevision = 0;
  cache.sortRevision = 0;
  cache.usesVertexPool = false;
  cache.poolVbo = 0;
  cache.poolEbo = 0;
  cache.VertexPool.Destroy();
  if (cache.IndirectCmdsBuffer)
  {
    glDeleteBuffers(1, &cache.IndirectCmdsBuffer);
    cache.IndirectCmdsBuffer = 0;
    cache.IndirectCmdCapacity = 0;
  }
  if (cache.BatchSphereSsbo)
  {
    glDeleteBuffers(1, &cache.BatchSphereSsbo);
    cache.BatchSphereSsbo = 0;
    cache.BatchSphereCapacity = 0;
  }
  if (cache.CullVisSsbo)
  {
    glDeleteBuffers(1, &cache.CullVisSsbo);
    cache.CullVisSsbo = 0;
    cache.CullVisCapacity = 0;
  }
  if (cache.CullAabbMaxSsbo)
  {
    glDeleteBuffers(1, &cache.CullAabbMaxSsbo);
    cache.CullAabbMaxSsbo = 0;
    cache.CullAabbMaxCapacity = 0;
  }
  cache.batchTableRevision = 0;
  cache.LastGoodCullOn = 0;
  cache.ConsecutiveFailOpenN = 0;
  cache.FailOpenProbeTick = 0;
  cache.IndirectCullReady = false;
  cache.GpuCompactActive = false;
}

void UGreedyGpuBackend::DestroyAll(GreedyGpuPassCache &opaque,
                                   GreedyGpuPassCache &cutout,
                                   GreedyGpuPassCache &transparent)
{
  DestroyPass(opaque);
  DestroyPass(cutout);
  DestroyPass(transparent);
}

} // namespace cutum
