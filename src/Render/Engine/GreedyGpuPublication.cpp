#include "Render/Camera/GpuPassRefreshPolicy.h"
#include "Render/Engine/GreedyGpuBackend.h"
#include "Render/GlIncludes.h"
#include <unordered_map>

namespace cutum
{
namespace
{
struct GpuBatchKey
{
  glm::ivec3 coord{0};
  uint16_t batchIndex{0};

  bool operator==(const GpuBatchKey &o) const
  {
    return coord == o.coord && batchIndex == o.batchIndex;
  }
};

struct GpuBatchKeyHash
{
  size_t operator()(const GpuBatchKey &k) const noexcept
  {
    size_t h = IVec3Hash{}(k.coord);
    h ^= static_cast<size_t>(k.batchIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

} // namespace
void UGreedyGpuBackend::DestroyBatchBuffers(GreedyGpuBatch &batch)
{
  if (batch.pooled)
  {
    batch.pooled = false;
    batch.vboByteOffset = 0;
    batch.eboByteOffset = 0;
    return;
  }
  if (batch.ebo != 0)
  {
    glDeleteBuffers(1, &batch.ebo);
    batch.ebo = 0;
  }
  if (batch.vbo != 0)
  {
    glDeleteBuffers(1, &batch.vbo);
    batch.vbo = 0;
  }
  batch.vboCapacityBytes = 0;
  batch.eboCapacityBytes = 0;
}

void UGreedyGpuBackend::ReleasePooledBatch(GreedyGpuBatch &batch,
                                           UGreedyVertexPool &pool)
{
  if (batch.pooled && batch.vertexCount > 0 && batch.indexCount > 0)
  {
    GreedyGpuPoolAllocation alloc;
    alloc.vertexByteOffset = batch.vboByteOffset;
    alloc.indexByteOffset = batch.eboByteOffset;
    alloc.vertexCount = batch.vertexCount;
    alloc.indexCount = batch.indexCount;
    pool.Free(alloc);
  }
  DestroyBatchBuffers(batch);
}

void UGreedyGpuBackend::FillBatchCull(GreedyGpuBatch &dst,
                                      const GreedyBatchRef &ref)
{
  dst.chunkCoord = ref.chunkCoord;
  dst.batchIndex = ref.batchIndex;
  FillChunkCullFields(ref.chunkCoord, dst.cullSphere, dst.cullAabbMin,
                      dst.cullAabbMax);
  dst.drawInstanceCount = 1;
}

void UGreedyGpuBackend::UploadBatch(GreedyGpuBatch &gpu,
                                    const GreedyMeshBatch &batch,
                                    UGreedyVertexPool &pool)
{
  const GreedyGpuBatch previous = gpu;
  GreedyGpuPoolAllocation prior{};
  if (gpu.pooled && gpu.vertexCount > 0 && gpu.indexCount > 0)
  {
    prior.vertexByteOffset = gpu.vboByteOffset;
    prior.indexByteOffset = gpu.eboByteOffset;
    prior.vertexCount = gpu.vertexCount;
    prior.indexCount = gpu.indexCount;
    prior.indexCountGl = gpu.indexCountGl;
  }

  gpu.blockId = batch.blockId;
  gpu.pooled = false;
  gpu.vboByteOffset = 0;
  gpu.eboByteOffset = 0;
  gpu.drawInstanceCount = 1;
  if (!batch.vertices.empty() && !batch.indices.empty())
  {
    const GreedyGpuPoolAllocation alloc = pool.Allocate(batch);
    if (alloc.vertexCount > 0 && alloc.indexCount > 0)
    {
      gpu.pooled = true;
      gpu.vboByteOffset = alloc.vertexByteOffset;
      gpu.eboByteOffset = alloc.indexByteOffset;
      gpu.vertexCount = alloc.vertexCount;
      gpu.indexCount = alloc.indexCount;
      gpu.indexCountGl = alloc.indexCountGl;
      gpu.vbo = pool.VertexBuffer();
      gpu.ebo = pool.IndexBuffer();
      if (prior.vertexCount > 0)
      {
        pool.Free(prior);
      }
      return;
    }
  }
  if (prior.vertexCount > 0)
  {
    // Retain metadata too: an allocation failure cannot change its material.
    gpu = previous;
    gpu.vbo = pool.VertexBuffer();
    gpu.ebo = pool.IndexBuffer();
    return;
  }
  gpu.vertexCount = 0;
  gpu.indexCount = 0;
  gpu.indexCountGl = 0;
}

bool UGreedyGpuBackend::PublishPassInputs(
    GreedyGpuPassCache &cache, const std::vector<GreedyGpuUploadInput> &inputs,
    const std::unordered_set<glm::ivec3, IVec3Hash> &dirty,
    uint64_t mesh_revision, uint64_t cull_revision, uint64_t sort_revision)
{
  cache.PendingGeometryDirty.insert(dirty.begin(), dirty.end());
  std::unordered_map<GpuBatchKey, size_t, GpuBatchKeyHash> resident;
  for (size_t n = 0; n < cache.batches.size(); ++n)
    resident.emplace(
        GpuBatchKey{cache.batches[n].chunkCoord, cache.batches[n].batchIndex},
        n);

  // Stage the entire command table, but upload only missing/dirty geometry.
  // No old allocation is released until every replacement has succeeded.
  std::vector<GreedyGpuBatch> staged;
  std::vector<size_t> fresh;
  std::vector<bool> retained(cache.batches.size(), false);
  staged.reserve(inputs.size());
  bool changed = cache.sortRevision != sort_revision;
  auto sync_handles = [&]()
  {
    cache.poolVbo = cache.VertexPool.VertexBuffer();
    cache.poolEbo = cache.VertexPool.IndexBuffer();
    for (auto &gpu : cache.batches)
      if (gpu.pooled)
      {
        gpu.vbo = cache.poolVbo;
        gpu.ebo = cache.poolEbo;
      }
  };

  for (const auto &input : inputs)
  {
    const auto &ref = input.ref;
    const auto *batch = input.batch;
    if (!batch || batch->vertices.empty() || batch->indices.empty())
      continue;
    const auto found = resident.find({ref.chunkCoord, ref.batchIndex});
    if (found != resident.end() &&
        cache.PendingGeometryDirty.count(ref.chunkCoord) == 0 &&
        cache.batches[found->second].pooled)
    {
      retained[found->second] = true;
      if (found->second != staged.size())
        changed = true;
      staged.push_back(cache.batches[found->second]);
      if (BatchCullAabbDegenerate(staged.back().cullAabbMin,
                                  staged.back().cullAabbMax))
      {
        FillBatchCull(staged.back(), ref);
        changed = true;
      }
      continue;
    }
    GreedyGpuBatch gpu;
    UploadBatch(gpu, *batch, cache.VertexPool);
    if (!gpu.pooled)
    {
      for (const size_t n : fresh)
        ReleasePooledBatch(staged[n], cache.VertexPool);
      sync_handles(); // Partial buffer growth preserves old bytes, changes IDs.
      return false;   // Retain old publication and dirty demand.
    }
    FillBatchCull(gpu, ref);
    fresh.push_back(staged.size());
    staged.push_back(gpu);
    changed = true;
  }
  changed = changed || staged.size() != cache.batches.size();
  for (size_t n = 0; n < cache.batches.size(); ++n)
    if (!retained[n])
      ReleasePooledBatch(cache.batches[n], cache.VertexPool);
  cache.batches = std::move(staged);
  cache.PendingGeometryDirty.clear();
  cache.usesVertexPool = !cache.batches.empty();
  sync_handles();
  cache.meshRevision = mesh_revision;
  cache.cullRevision = cull_revision;
  cache.sortRevision = sort_revision;
  if (changed)
  {
    cache.IndirectCullReady = false;
    cache.GpuCompactActive = false;
    cache.CompactVisCpuSynced = false;
    ++cache.publicationVersion;
  }
  cache.VertexPool.SignalUploadComplete();
  return true;
}
} // namespace cutum
