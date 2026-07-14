#include "Render/Engine/GreedyGpuBackend.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Engine/GreedyVertexPool.h"
#include "Render/GlIncludes.h"

namespace cutum
{

namespace
{

constexpr unsigned int kArrayBuffer = GL_ARRAY_BUFFER;
constexpr unsigned int kElementArrayBuffer = GL_ELEMENT_ARRAY_BUFFER;

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

void UGreedyGpuBackend::UploadBatch(GreedyGpuBatch &gpu,
                                    const GreedyMeshBatch &batch,
                                    UGreedyVertexPool &pool)
{
  gpu.blockId = batch.blockId;
  gpu.pooled = false;
  gpu.vboByteOffset = 0;
  gpu.eboByteOffset = 0;
  if (!batch.vertices.empty() && !batch.indices.empty())
  {
    const GreedyGpuPoolAllocation alloc = pool.Allocate(batch);
    if (alloc.vertexCount > 0 && alloc.indexCount > 0)
    {
      gpu.vertexCount = alloc.vertexCount;
      gpu.indexCount = alloc.indexCount;
      gpu.indexCountGl = alloc.indexCountGl;
      gpu.pooled = true;
      gpu.vboByteOffset = alloc.vertexByteOffset;
      gpu.eboByteOffset = alloc.indexByteOffset;
      return;
    }
  }

  gpu.vertexCount = batch.vertices.size();
  gpu.indexCount = batch.indices.size();
  gpu.indexCountGl = static_cast<GLsizei>(batch.indices.size());

  const size_t vbo_bytes = batch.vertices.size() * sizeof(GreedyMeshVertex);
  const size_t ebo_bytes = batch.indices.size() * sizeof(uint32_t);
  UploadBuffer(gpu.vbo, gpu.vboCapacityBytes, kArrayBuffer,
               batch.vertices.data(), vbo_bytes);
  UploadBuffer(gpu.ebo, gpu.eboCapacityBytes, kElementArrayBuffer,
               batch.indices.data(), ebo_bytes);
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
  cache.meshRevision = mesh_revision;
  cache.cullRevision = cull_revision;
  cache.sortRevision = sort_revision;
}

void UGreedyGpuBackend::RefreshPassRefs(
    GreedyGpuPassCache &cache, const UChunkMeshCache &meshCache,
    const std::vector<GreedyBatchRef> &refs,
    uint64_t mesh_revision, uint64_t cull_revision, uint64_t sort_revision)
{
  if (mesh_revision == cache.meshRevision &&
      cull_revision == cache.cullRevision &&
      sort_revision == cache.sortRevision)
  {
    return;
  }

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
    if (write_index < cache.batches.size())
    {
      UploadBatch(cache.batches[write_index], *batch, cache.VertexPool);
      ++write_index;
      continue;
    }
    GreedyGpuBatch gpu;
    UploadBatch(gpu, *batch, cache.VertexPool);
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
  cache.meshRevision = mesh_revision;
  cache.cullRevision = cull_revision;
  cache.sortRevision = sort_revision;
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
