#include "Render/Engine/CrossGpuBackend.h"
#include "Render/GlIncludes.h"
#include "Render/Mesh/CrossInstanceBatch.h"
#include "Render/Mesh/CrossMeshEmitter.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "World/Mesh/Contracts/MeshPublishContract.h"
#include <algorithm>
#include <cstddef>
#include <glm/glm.hpp>
#include <limits>
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
  if (Templates[templateIndex].vao)
    return Templates[templateIndex].vbo && Templates[templateIndex].ebo &&
           Templates[templateIndex].indexCount > 0;
  if (glGetError() != GL_NO_ERROR)
    return false;
  TemplateMesh mesh;

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
  glBufferData(
      kArrayBuffer,
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
  if (!mesh.vao || !mesh.vbo || !mesh.ebo || glGetError() != GL_NO_ERROR)
  {
    if (mesh.vao)
      glDeleteVertexArrays(1, &mesh.vao);
    if (mesh.vbo)
      glDeleteBuffers(1, &mesh.vbo);
    if (mesh.ebo)
      glDeleteBuffers(1, &mesh.ebo);
    return false;
  }
  Templates[templateIndex] = mesh;
  return true;
}

bool UCrossGpuBackend::UploadBuffer(GLuint &buffer, size_t &capacity_bytes,
                                    unsigned int target, const void *data,
                                    size_t byte_size)
{
  if (!byte_size ||
      byte_size > static_cast<size_t>(std::numeric_limits<GLsizeiptr>::max()) ||
      glGetError() != GL_NO_ERROR)
    return false;
  if (!buffer)
    glGenBuffers(1, &buffer);
  if (!buffer)
    return false;
  glBindBuffer(target, buffer);
  // RefreshPass retires scratch with a fence. Reuse its actual store only
  // after queued readers finish, rather than accumulating orphaned stores.
  if (byte_size > capacity_bytes)
  {
    glBufferData(target, static_cast<GLsizeiptr>(byte_size), nullptr,
                 GL_DYNAMIC_DRAW);
    if (glGetError() != GL_NO_ERROR)
      return false;
    capacity_bytes = byte_size;
  }
  if (BeforeTransfer)
    BeforeTransfer(target, TransferContext);
  glBufferSubData(target, 0, static_cast<GLsizeiptr>(byte_size), data);
  return glGetError() == GL_NO_ERROR;
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
  batch.lightRanges.clear();
}

bool UCrossGpuBackend::EnsureBatchVao(CrossGpuBatch &gpu)
{
  if (!EnsureTemplateMesh(gpu.shape) || gpu.instanceVbo == 0)
    return false;
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
  BindInstanceAttributes(0);
  glBindVertexArray(0);
  glBindBuffer(kArrayBuffer, 0);
  glBindBuffer(kElementArrayBuffer, 0);
  return gpu.vao && glGetError() == GL_NO_ERROR;
}

void UCrossGpuBackend::BindInstanceAttributes(size_t first)
{
  const size_t base = first * sizeof(CrossInstanceGpu);
  constexpr GLsizei kInstanceStride =
      static_cast<GLsizei>(sizeof(CrossInstanceGpu));
  glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, kInstanceStride,
                        reinterpret_cast<void *>(base));
  glEnableVertexAttribArray(3);
  glVertexAttribDivisor(3, 1);
  glVertexAttribPointer(
      4, 1, GL_FLOAT, GL_FALSE, kInstanceStride,
      reinterpret_cast<void *>(base + offsetof(CrossInstanceGpu, skyLight)));
  glEnableVertexAttribArray(4);
  glVertexAttribDivisor(4, 1);
  glVertexAttribPointer(
      5, 1, GL_FLOAT, GL_FALSE, kInstanceStride,
      reinterpret_cast<void *>(base + offsetof(CrossInstanceGpu, blockLight)));
  glEnableVertexAttribArray(5);
  glVertexAttribDivisor(5, 1);
  glVertexAttribIPointer(
      6, 4, GL_UNSIGNED_INT, kInstanceStride,
      reinterpret_cast<void *>(base +
                               offsetof(CrossInstanceGpu, renderLightHandle)));
  glEnableVertexAttribArray(6);
  glVertexAttribDivisor(6, 1);
}

bool UCrossGpuBackend::DrawRange(const CrossGpuBatch &batch, size_t first,
                                 size_t count)
{
  if (!batch.vao || !batch.instanceVbo || !count ||
      first > batch.instanceCount || count > batch.instanceCount - first ||
      count > static_cast<size_t>(std::numeric_limits<GLsizei>::max()) ||
      first > std::numeric_limits<size_t>::max() / sizeof(CrossInstanceGpu))
    return false;
  glBindVertexArray(batch.vao);
  glBindBuffer(kArrayBuffer, batch.instanceVbo);
  BindInstanceAttributes(first);
  glDrawElementsInstanced(GL_TRIANGLES, GetTemplateIndexCount(batch.shape),
                          GL_UNSIGNED_INT, nullptr,
                          static_cast<GLsizei>(count));
  BindInstanceAttributes(0);
  glBindVertexArray(0);
  glBindBuffer(kArrayBuffer, 0);
  return true;
}

bool UCrossGpuBackend::UploadInstances(CrossGpuBatch &gpu,
                                       const CrossInstanceBatch &batch)
{
  if (batch.instances.size() >
          static_cast<size_t>(std::numeric_limits<GLsizei>::max()) ||
      batch.instances.size() >
          std::numeric_limits<size_t>::max() / sizeof(CrossInstanceGpu))
    return false;
  const size_t byte_size = batch.instances.size() * sizeof(CrossInstanceGpu);
  if (!UploadBuffer(gpu.instanceVbo, gpu.instanceCapacityBytes, kArrayBuffer,
                    batch.instances.data(), byte_size))
    return false;
  gpu.shape = batch.shape;
  if (!EnsureBatchVao(gpu))
    return false;
  gpu.blockId = batch.blockId;
  gpu.instanceCount = batch.instances.size();
  gpu.lightRanges = batch.lightRanges;
  return true;
}

bool UCrossGpuBackend::RefreshPass(
    CrossGpuPassCache &cache, const std::vector<CrossInstanceBatch> &batches,
    uint64_t candidate_mesh_rev, uint64_t candidate_cull_rev,
    uint64_t expected_mesh_rev, uint64_t expected_cull_rev,
    const std::vector<uint8_t> &instance_selection)
{
  // A37 H3: reject stale/backwards refresh.
  if (cache.meshRevision != 0 && candidate_mesh_rev < cache.meshRevision)
  {
    return false;
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
      return false; // keep prior resident
    }
  }

  if ((cache.meshRevision || cache.cullRevision || !cache.batches.empty()) &&
      candidate_mesh_rev == cache.meshRevision &&
      candidate_cull_rev == cache.cullRevision &&
      instance_selection == cache.instanceSelection)
    return true;
  if (glGetError() != GL_NO_ERROR)
    return false;
  if (StagingFence)
  {
    const auto status =
        glClientWaitSync(static_cast<GLsync>(StagingFence), 0, 0);
    if (status == GL_TIMEOUT_EXPIRED)
      return false; // Never stall a paint waiting to replace a valid pass.
    glDeleteSync(static_cast<GLsync>(StagingFence));
    StagingFence = nullptr;
    if (status == GL_WAIT_FAILED)
    {
      DestroyPass(Staging);
      glGetError();
      return false;
    }
  }

  const auto release_staged_leases = [&]()
  {
    for (auto &batch : Staging.batches)
      batch.lightRanges.clear();
    glBindVertexArray(0);
    glBindBuffer(kArrayBuffer, 0);
  };
  try
  {
    // Allocate the selection before publication so allocation failure also
    // preserves the complete predecessor and its selection identity.
    Staging.instanceSelection = instance_selection;
    const size_t count = static_cast<size_t>(std::count_if(
        batches.begin(), batches.end(), [](const CrossInstanceBatch &batch)
        { return !batch.instances.empty(); }));
    for (size_t n = count; n < Staging.batches.size(); ++n)
      DestroyInstanceBuffer(Staging.batches[n]);
    Staging.batches.resize(count);
    size_t write_index = 0;
    for (const auto &batch : batches)
      if (!batch.instances.empty())
      {
        if (!UploadInstances(Staging.batches[write_index++], batch))
        {
          release_staged_leases();
          return false;
        }
      }
  }
  catch (...)
  {
    release_staged_leases();
    return false;
  }
  // Only this swap publishes new geometry and its matching field ownership.
  cache.batches.swap(Staging.batches);
  cache.instanceSelection.swap(Staging.instanceSelection);
  if (!Staging.batches.empty())
  {
    StagingFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!StagingFence)
    {
      // Deletion preserves queued GL readers even if retirement cannot be
      // tracked. Do not reuse unprotected storage after a failed fence.
      DestroyPass(Staging);
      glGetError();
    }
  }
  release_staged_leases(); // Scratch is never drawn; old queued draws precede
                           // release.
  cache.meshRevision = candidate_mesh_rev;
  cache.cullRevision = candidate_cull_rev;
  return true;
}

void UCrossGpuBackend::DestroyPass(CrossGpuPassCache &cache)
{
  for (CrossGpuBatch &batch : cache.batches)
  {
    DestroyInstanceBuffer(batch);
  }
  cache.batches.clear();
  cache.instanceSelection.clear();
  cache.meshRevision = 0;
  cache.cullRevision = 0;
}

void UCrossGpuBackend::DestroyAll(CrossGpuPassCache &cache)
{
  if (StagingFence)
    glDeleteSync(static_cast<GLsync>(StagingFence));
  StagingFence = nullptr;
  DestroyPass(cache);
  DestroyPass(Staging);
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
