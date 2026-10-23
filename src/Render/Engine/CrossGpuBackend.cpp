#include "Render/Engine/CrossGpuBackend.h"
#include "Render/GlIncludes.h"
#include "Render/Mesh/CrossInstanceBatch.h"
#include "Render/Mesh/CrossMeshEmitter.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "Render/Mesh/MeshPublishContract.h"
#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

namespace
{

constexpr unsigned int kArrayBuffer = GL_ARRAY_BUFFER;
constexpr unsigned int kElementArrayBuffer = GL_ELEMENT_ARRAY_BUFFER;

} // namespace

bool UCrossGpuBackend::EnsureTemplateMesh()
{
  for (int i = 0; i < static_cast<int>(Templates.size()); ++i)
  {
    if (!EnsureTemplateMesh(static_cast<BlockDecorShape>(i)))
    {
      return false;
    }
  }
  return true;
}

GLsizei UCrossGpuBackend::GetTemplateIndexCount(BlockDecorShape shape) const
{
  const size_t index = static_cast<size_t>(shape);
  return index < Templates.size() ? Templates[index].indexCount : 0;
}

bool UCrossGpuBackend::EnsureTemplateMesh(BlockDecorShape shape)
{
  const size_t templateIndex = static_cast<size_t>(shape);
  if (templateIndex >= Templates.size())
  {
    return false;
  }
  TemplateMesh &mesh = Templates[templateIndex];
  if (mesh.vao != 0)
  {
    return true;
  }

  std::vector<GreedyMeshVertex> vertices;
  std::vector<uint32_t> indices;
  AppendDecorSprite(shape, glm::vec3(0.0f), vertices, indices);
  if (vertices.empty() || indices.empty())
  {
    return false;
  }

  glGenVertexArrays(1, &mesh.vao);
  glGenBuffers(1, &mesh.vbo);
  glGenBuffers(1, &mesh.ebo);

  glBindVertexArray(mesh.vao);
  glBindBuffer(kArrayBuffer, mesh.vbo);
  glBufferData(kArrayBuffer,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(GreedyMeshVertex)),
               vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(kElementArrayBuffer, mesh.ebo);
  glBufferData(kElementArrayBuffer,
               static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
               indices.data(), GL_STATIC_DRAW);

  constexpr GLsizei kStride = static_cast<GLsizei>(sizeof(GreedyMeshVertex));
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, kStride,
                        (void *)(offsetof(GreedyMeshVertex, faceIndex)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride,
                        (void *)(offsetof(GreedyMeshVertex, u)));
  glEnableVertexAttribArray(2);

  mesh.indexCount = static_cast<GLsizei>(indices.size());
  glBindVertexArray(0);
  glBindBuffer(kArrayBuffer, 0);
  glBindBuffer(kElementArrayBuffer, 0);
  return mesh.vao != 0;
}

void UCrossGpuBackend::UploadBuffer(GLuint &buffer, size_t &capacity_bytes,
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

void UCrossGpuBackend::DestroyInstanceBuffer(CrossGpuBatch &batch)
{
  if (batch.vao != 0)
  {
    glDeleteVertexArrays(1, &batch.vao);
    batch.vao = 0;
  }
  if (batch.instanceVbo != 0)
  {
    glDeleteBuffers(1, &batch.instanceVbo);
    batch.instanceVbo = 0;
  }
  batch.instanceCapacityBytes = 0;
  batch.instanceCount = 0;
}

void UCrossGpuBackend::EnsureBatchVao(CrossGpuBatch &gpu)
{
  if (!EnsureTemplateMesh(gpu.shape) || gpu.instanceVbo == 0)
  {
    return;
  }
  if (gpu.vao != 0 && gpu.vaoShape != gpu.shape)
  {
    glDeleteVertexArrays(1, &gpu.vao);
    gpu.vao = 0;
  }
  if (gpu.vao == 0)
  {
    glGenVertexArrays(1, &gpu.vao);
  }
  gpu.vaoShape = gpu.shape;
  const TemplateMesh &mesh = Templates[static_cast<size_t>(gpu.shape)];
  glBindVertexArray(gpu.vao);
  glBindBuffer(kArrayBuffer, mesh.vbo);
  constexpr GLsizei kStride = static_cast<GLsizei>(sizeof(GreedyMeshVertex));
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, kStride,
                        (void *)(offsetof(GreedyMeshVertex, faceIndex)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride,
                        (void *)(offsetof(GreedyMeshVertex, u)));
  glEnableVertexAttribArray(2);
  glBindBuffer(kElementArrayBuffer, mesh.ebo);
  glBindBuffer(kArrayBuffer, gpu.instanceVbo);
  constexpr GLsizei kInstanceStride =
      static_cast<GLsizei>(sizeof(CrossInstanceGpu));
  glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, kInstanceStride, (void *)0);
  glEnableVertexAttribArray(3);
  glVertexAttribDivisor(3, 1);
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, kInstanceStride,
                        (void *)(offsetof(CrossInstanceGpu, skyLight)));
  glEnableVertexAttribArray(4);
  glVertexAttribDivisor(4, 1);
  glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, kInstanceStride,
                        (void *)(offsetof(CrossInstanceGpu, blockLight)));
  glEnableVertexAttribArray(5);
  glVertexAttribDivisor(5, 1);
  glBindVertexArray(0);
  glBindBuffer(kArrayBuffer, 0);
  glBindBuffer(kElementArrayBuffer, 0);
}

void UCrossGpuBackend::UploadInstances(CrossGpuBatch &gpu,
                                       const CrossInstanceBatch &batch)
{
  gpu.blockId = batch.blockId;
  gpu.shape = batch.shape;
  gpu.instanceCount = batch.instances.size();
  const size_t byte_size =
      batch.instances.size() * sizeof(CrossInstanceGpu);
  UploadBuffer(gpu.instanceVbo, gpu.instanceCapacityBytes, kArrayBuffer,
               batch.instances.data(), byte_size);
  EnsureBatchVao(gpu);
}

void UCrossGpuBackend::RefreshPass(
    CrossGpuPassCache &cache, const std::vector<CrossInstanceBatch> &batches,
    uint64_t candidate_mesh_rev, uint64_t candidate_cull_rev,
    uint64_t expected_mesh_rev, uint64_t expected_cull_rev)
{
  if (candidate_mesh_rev == cache.meshRevision &&
      candidate_cull_rev == cache.cullRevision)
  {
    return;
  }
  // A37 H3: reject stale/backwards refresh.
  if (cache.meshRevision != 0 && candidate_mesh_rev < cache.meshRevision)
  {
    return;
  }
  // A39 P5: got = candidate; expected = live desire (NOT a copy of got).
  // Epochs left zero so TableEpochStale does not block intentional forward
  // refresh; SourceMismatch rejects when candidate ≠ live desire.
  {
    const PublicationValidation v = ValidateCrossOrShellPublication(
        /*got_geom=*/candidate_mesh_rev, /*got_light=*/candidate_cull_rev,
        /*light_valid=*/true,
        /*expected_geom=*/expected_mesh_rev,
        /*expected_light=*/expected_cull_rev);
    if (!PublicationCandidateAccepted(v))
    {
      return; // keep prior resident
    }
  }

  size_t write_index = 0;
  for (const CrossInstanceBatch &batch : batches)
  {
    if (batch.instances.empty())
    {
      continue;
    }
    if (write_index < cache.batches.size())
    {
      UploadInstances(cache.batches[write_index], batch);
      ++write_index;
      continue;
    }
    CrossGpuBatch gpu;
    UploadInstances(gpu, batch);
    cache.batches.push_back(gpu);
    ++write_index;
  }

  for (size_t i = write_index; i < cache.batches.size(); ++i)
  {
    DestroyInstanceBuffer(cache.batches[i]);
  }
  cache.batches.resize(write_index);

  glBindBuffer(kArrayBuffer, 0);
  cache.meshRevision = candidate_mesh_rev;
  cache.cullRevision = candidate_cull_rev;
}

void UCrossGpuBackend::DestroyPass(CrossGpuPassCache &cache)
{
  for (CrossGpuBatch &batch : cache.batches)
  {
    DestroyInstanceBuffer(batch);
  }
  cache.batches.clear();
  cache.meshRevision = 0;
  cache.cullRevision = 0;
}

void UCrossGpuBackend::DestroyAll(CrossGpuPassCache &cache)
{
  DestroyPass(cache);
  for (TemplateMesh &mesh : Templates)
  {
    if (mesh.ebo != 0)
    {
      glDeleteBuffers(1, &mesh.ebo);
      mesh.ebo = 0;
    }
    if (mesh.vbo != 0)
    {
      glDeleteBuffers(1, &mesh.vbo);
      mesh.vbo = 0;
    }
    if (mesh.vao != 0)
    {
      glDeleteVertexArrays(1, &mesh.vao);
      mesh.vao = 0;
    }
    mesh.indexCount = 0;
  }
}

} // namespace cutum
