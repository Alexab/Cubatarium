#include "Render/Engine/GreedyGpuBackend.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Engine/GreedyVertexPool.h"
#include "Render/Camera/Frustum.h"
#include "Render/Camera/GpuPassRefreshPolicy.h"
#include "Render/GlIncludes.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/RuntimeTuning.h"
#include <algorithm>
#include <unordered_set>

namespace cutum
{

namespace
{

constexpr unsigned int kArrayBuffer = GL_ARRAY_BUFFER;
constexpr unsigned int kElementArrayBuffer = GL_ELEMENT_ARRAY_BUFFER;

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
  FillChunkCullFields(ref.chunkCoord, dst.cullSphere, dst.cullAabbMin,
                      dst.cullAabbMax);
  dst.drawInstanceCount = 1;
}

void UGreedyGpuBackend::UploadBatch(GreedyGpuBatch &gpu,
                                    const GreedyMeshBatch &batch,
                                    UGreedyVertexPool &pool)
{
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
      return;
    }
  }
  gpu.vertexCount = 0;
  gpu.indexCount = 0;
  gpu.indexCountGl = 0;
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

  size_t total_vertex_bytes = 0;
  size_t total_index_bytes = 0;
  for (const GreedyMeshBatch &batch : batches)
  {
    if (batch.vertices.empty() || batch.indices.empty())
    {
      continue;
    }
    total_vertex_bytes += batch.vertices.size() * sizeof(GreedyMeshVertex);
    total_index_bytes += batch.indices.size() * sizeof(uint32_t);
  }
  ApplyPoolBudget(cache.VertexPool);
  cache.VertexPool.Reserve(total_vertex_bytes, total_index_bytes);
  cache.usesVertexPool = total_vertex_bytes > 0 && total_index_bytes > 0;
  cache.poolVbo = cache.VertexPool.VertexBuffer();
  cache.poolEbo = cache.VertexPool.IndexBuffer();

  size_t write_index = 0;
  for (const GreedyMeshBatch &batch : batches)
  {
    if (batch.vertices.empty() || batch.indices.empty())
    {
      continue;
    }
    if (write_index < cache.batches.size())
    {
      UploadBatch(cache.batches[write_index], batch, cache.VertexPool);
      ++write_index;
      continue;
    }
    GreedyGpuBatch gpu;
    UploadBatch(gpu, batch, cache.VertexPool);
    cache.batches.push_back(gpu);
    ++write_index;
  }

  for (size_t i = write_index; i < cache.batches.size(); ++i)
  {
    DestroyBatchBuffers(cache.batches[i]);
  }
  cache.batches.resize(write_index);

  glBindBuffer(kArrayBuffer, 0);
  glBindBuffer(kElementArrayBuffer, 0);
  cache.VertexPool.SignalUploadComplete();
  cache.meshRevision = mesh_revision;
  cache.cullRevision = cull_revision;
  cache.sortRevision = sort_revision;
}

void UGreedyGpuBackend::RefreshPassRefs(
    GreedyGpuPassCache &cache, const UChunkMeshCache &meshCache,
    const std::vector<GreedyBatchRef> &refs,
    uint64_t mesh_revision, uint64_t cull_revision, uint64_t sort_revision)
{
  // P2: CullRevision only invalidates draw instance counts, not geometry.
  // Exception: degenerate AABB (warmup upload skipped FillBatchCull) or a
  // disjoint visible set after teleport / first paint — rebuild or repair.
  {
    std::unordered_set<glm::ivec3, IVec3Hash> gpu_chunks;
    gpu_chunks.reserve(cache.batches.size());
    bool degenerate = false;
    for (const GreedyGpuBatch &b : cache.batches)
    {
      if (!b.pooled || b.indexCountGl <= 0)
      {
        continue;
      }
      gpu_chunks.insert(b.chunkCoord);
      if (BatchCullAabbDegenerate(b.cullAabbMin, b.cullAabbMax))
      {
        degenerate = true;
      }
    }
    size_t visible_refs = 0;
    size_t overlap = 0;
    for (const GreedyBatchRef &ref : refs)
    {
      ++visible_refs;
      if (gpu_chunks.count(ref.chunkCoord) > 0)
      {
        ++overlap;
      }
    }
    const bool need_rebuild = GpuPassVisibleSetNeedsRebuild(
        overlap, visible_refs, cache.batches.size());
    if (mesh_revision == cache.meshRevision &&
        sort_revision == cache.sortRevision && !need_rebuild)
    {
      if (degenerate)
      {
        size_t write_index = 0;
        for (const GreedyBatchRef &ref : refs)
        {
          const GreedyMeshBatch *batch = meshCache.TryGetGreedyBatch(ref);
          if (!batch || batch->vertices.empty() || batch->indices.empty())
          {
            continue;
          }
          if (write_index < cache.batches.size())
          {
            FillBatchCull(cache.batches[write_index], ref);
          }
          ++write_index;
        }
        cache.cullRevision = cull_revision;
        cache.IndirectCullReady = false;
        cache.GpuCompactActive = false;
        return;
      }
      cache.cullRevision = cull_revision;
      return;
    }
  }

  std::unordered_set<glm::ivec3, IVec3Hash> dirty;
  meshCache.ConsumeGeometryDirtyChunks(dirty);

  constexpr size_t kMaxIncrementalDirty = 48;
  const bool sort_changed = sort_revision != cache.sortRevision;
  const bool can_incremental =
      cache.usesVertexPool && cache.VertexPool.IsActive() && !sort_changed &&
      !dirty.empty() && dirty.size() <= kMaxIncrementalDirty;

  auto upload_full = [&]()
  {
    size_t total_vertex_bytes = 0;
    size_t total_index_bytes = 0;
    for (const GreedyBatchRef &ref : refs)
    {
      const GreedyMeshBatch *batch = meshCache.TryGetGreedyBatch(ref);
      if (!batch || batch->vertices.empty() || batch->indices.empty())
      {
        continue;
      }
      total_vertex_bytes += batch->vertices.size() * sizeof(GreedyMeshVertex);
      total_index_bytes += batch->indices.size() * sizeof(uint32_t);
    }
    ApplyPoolBudget(cache.VertexPool);
    cache.VertexPool.Reserve(total_vertex_bytes, total_index_bytes);
    cache.usesVertexPool = total_vertex_bytes > 0 && total_index_bytes > 0;
    cache.poolVbo = cache.VertexPool.VertexBuffer();
    cache.poolEbo = cache.VertexPool.IndexBuffer();

    size_t write_index = 0;
    for (const GreedyBatchRef &ref : refs)
    {
      const GreedyMeshBatch *batch = meshCache.TryGetGreedyBatch(ref);
      if (!batch || batch->vertices.empty() || batch->indices.empty())
      {
        continue;
      }
      GreedyGpuBatch *dst = nullptr;
      if (write_index < cache.batches.size())
      {
        dst = &cache.batches[write_index];
        UploadBatch(*dst, *batch, cache.VertexPool);
      }
      else
      {
        GreedyGpuBatch gpu;
        UploadBatch(gpu, *batch, cache.VertexPool);
        cache.batches.push_back(gpu);
        dst = &cache.batches.back();
      }
      FillBatchCull(*dst, ref);
      ++write_index;
    }

    for (size_t i = write_index; i < cache.batches.size(); ++i)
    {
      DestroyBatchBuffers(cache.batches[i]);
    }
    cache.batches.resize(write_index);
  };

  if (can_incremental)
  {
    std::unordered_set<glm::ivec3, IVec3Hash> live_chunks;
    live_chunks.reserve(refs.size());
    for (const GreedyBatchRef &ref : refs)
    {
      live_chunks.insert(ref.chunkCoord);
    }

    // Free dirty / unloaded slots back into the freelist.
    size_t write = 0;
    for (size_t i = 0; i < cache.batches.size(); ++i)
    {
      GreedyGpuBatch &b = cache.batches[i];
      const bool drop = dirty.count(b.chunkCoord) > 0 ||
                        live_chunks.count(b.chunkCoord) == 0;
      if (drop)
      {
        ReleasePooledBatch(b, cache.VertexPool);
        continue;
      }
      if (write != i)
      {
        cache.batches[write] = b;
      }
      ++write;
    }
    cache.batches.resize(write);

    ApplyPoolBudget(cache.VertexPool);
    const size_t cap_v_before = cache.VertexPool.VertexCapacityBytesValue();
    const size_t cap_before = cache.VertexPool.CapacityBytes();
    bool upload_ok = true;
    std::vector<GreedyGpuBatch> added;
    added.reserve(dirty.size() * 2);
    for (const GreedyBatchRef &ref : refs)
    {
      if (dirty.find(ref.chunkCoord) == dirty.end())
      {
        continue;
      }
      const GreedyMeshBatch *batch = meshCache.TryGetGreedyBatch(ref);
      if (!batch || batch->vertices.empty() || batch->indices.empty())
      {
        continue;
      }
      GreedyGpuBatch gpu;
      UploadBatch(gpu, *batch, cache.VertexPool);
      if (!gpu.pooled)
      {
        upload_ok = false;
        break;
      }
      FillBatchCull(gpu, ref);
      added.push_back(gpu);
    }
    // Allocate/EnsureCapacity may orphan the GL buffer on grow — full rewrite.
    if (!upload_ok ||
        cache.VertexPool.VertexCapacityBytesValue() != cap_v_before ||
        cache.VertexPool.CapacityBytes() != cap_before)
    {
      upload_full();
    }
    else
    {
      cache.batches.insert(cache.batches.end(), added.begin(), added.end());
      std::sort(cache.batches.begin(), cache.batches.end(),
                [](const GreedyGpuBatch &a, const GreedyGpuBatch &b)
                { return a.blockId < b.blockId; });
      cache.poolVbo = cache.VertexPool.VertexBuffer();
      cache.poolEbo = cache.VertexPool.IndexBuffer();
    }
  }
  else
  {
    upload_full();
  }

  glBindBuffer(kArrayBuffer, 0);
  glBindBuffer(kElementArrayBuffer, 0);
  cache.VertexPool.SignalUploadComplete();
  cache.meshRevision = mesh_revision;
  cache.cullRevision = cull_revision;
  cache.sortRevision = sort_revision;
  cache.IndirectCullReady = false;
}

void UGreedyGpuBackend::DestroyPass(GreedyGpuPassCache &cache)
{
  for (GreedyGpuBatch &batch : cache.batches)
  {
    DestroyBatchBuffers(batch);
  }
  cache.batches.clear();
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
