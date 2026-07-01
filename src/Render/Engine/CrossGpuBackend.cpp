#include "Render/Engine/CrossGpuBackend.h"
#include "Render/GlIncludes.h"
#include "Render/Mesh/CrossMeshEmitter.h"
#include "Render/Mesh/GreedyMeshVertex.h"
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
  if (TemplateVao != 0)
  {
    return true;
  }

  std::vector<GreedyMeshVertex> vertices;
  std::vector<uint32_t> indices;
  AppendCrossSprite(glm::vec3(0.0f), vertices, indices);
  if (vertices.empty() || indices.empty())
  {
    return false;
  }

  glGenVertexArrays(1, &TemplateVao);
  glGenBuffers(1, &TemplateVbo);
  glGenBuffers(1, &TemplateEbo);

  glBindVertexArray(TemplateVao);
  glBindBuffer(kArrayBuffer, TemplateVbo);
  glBufferData(kArrayBuffer,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(GreedyMeshVertex)),
               vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(kElementArrayBuffer, TemplateEbo);
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

  TemplateIndexCountGl = static_cast<GLsizei>(indices.size());
  glBindVertexArray(0);
  glBindBuffer(kArrayBuffer, 0);
  glBindBuffer(kElementArrayBuffer, 0);
  return TemplateVao != 0;
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
  if (!EnsureTemplateMesh() || gpu.instanceVbo == 0)
  {
    return;
  }
  if (gpu.vao == 0)
  {
    glGenVertexArrays(1, &gpu.vao);
  }
  glBindVertexArray(gpu.vao);
  glBindBuffer(kArrayBuffer, TemplateVbo);
  constexpr GLsizei kStride = static_cast<GLsizei>(sizeof(GreedyMeshVertex));
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, kStride,
                        (void *)(offsetof(GreedyMeshVertex, faceIndex)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride,
                        (void *)(offsetof(GreedyMeshVertex, u)));
  glEnableVertexAttribArray(2);
  glBindBuffer(kElementArrayBuffer, TemplateEbo);
  glBindBuffer(kArrayBuffer, gpu.instanceVbo);
  glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void *)0);
  glEnableVertexAttribArray(3);
  glVertexAttribDivisor(3, 1);
  glBindVertexArray(0);
  glBindBuffer(kArrayBuffer, 0);
  glBindBuffer(kElementArrayBuffer, 0);
}

void UCrossGpuBackend::UploadInstances(CrossGpuBatch &gpu,
                                       const CrossInstanceBatch &batch)
{
  gpu.blockId = batch.blockId;
  gpu.instanceCount = batch.centers.size();
  const size_t byte_size = batch.centers.size() * sizeof(glm::vec3);
  UploadBuffer(gpu.instanceVbo, gpu.instanceCapacityBytes, kArrayBuffer,
               batch.centers.data(), byte_size);
  EnsureBatchVao(gpu);
}

void UCrossGpuBackend::RefreshPass(
    CrossGpuPassCache &cache, const std::vector<CrossInstanceBatch> &batches,
    uint64_t mesh_revision, uint64_t cull_revision)
{
  if (mesh_revision == cache.meshRevision &&
      cull_revision == cache.cullRevision)
  {
    return;
  }

  size_t write_index = 0;
  for (const CrossInstanceBatch &batch : batches)
  {
    if (batch.centers.empty())
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
  cache.meshRevision = mesh_revision;
  cache.cullRevision = cull_revision;
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
  if (TemplateEbo != 0)
  {
    glDeleteBuffers(1, &TemplateEbo);
    TemplateEbo = 0;
  }
  if (TemplateVbo != 0)
  {
    glDeleteBuffers(1, &TemplateVbo);
    TemplateVbo = 0;
  }
  if (TemplateVao != 0)
  {
    glDeleteVertexArrays(1, &TemplateVao);
    TemplateVao = 0;
  }
  TemplateIndexCountGl = 0;
}

} // namespace cutum
