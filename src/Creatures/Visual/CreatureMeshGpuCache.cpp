#include "Creatures/Visual/CreatureMeshGpuCache.h"

#include "Creatures/Visual/CreatureMeshGpuHash.h"
#include "Render/GlIncludes.h"
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace cutum
{

namespace
{

/// Meshes built by the loaders carry their hash from build time; meshes from
/// elsewhere get a one-time lazy fill (idempotent write of the same value).
size_t SkeletalMeshKey(const BoneSkeletonCubeMeshCpu &mesh)
{
  if (mesh.GpuHash == 0)
  {
    mesh.GpuHash = SkeletalMeshGpuHash(mesh);
  }
  return mesh.GpuHash;
}

size_t GltfSkinnedMeshKey(const GltfPrimitiveCpu &mesh)
{
  if (mesh.GpuHash == 0)
  {
    mesh.GpuHash = GltfSkinnedMeshGpuHash(mesh);
  }
  return mesh.GpuHash;
}

} // namespace

CreatureMeshGpuCache &CreatureMeshGpuCache::Instance()
{
  static CreatureMeshGpuCache cache;
  return cache;
}

GLuint CreatureMeshGpuCache::GetOrCreateSkeletalMeshVao(
    const BoneSkeletonCubeMeshCpu &mesh)
{
  const size_t hash = SkeletalMeshKey(mesh);
  {
    std::lock_guard<std::mutex> lock(CacheMutex);
    if (const auto it = SkeletalCache.find(hash); it != SkeletalCache.end())
    {
      return it->second.vao;
    }
  }

  // GL object creation happens under the lock too: the previous unlock/create/
  // re-lock window allowed two threads to build (and leak) the same VAO.
  std::lock_guard<std::mutex> lock(CacheMutex);
  if (const auto it = SkeletalCache.find(hash); it != SkeletalCache.end())
  {
    return it->second.vao;
  }
  SkeletalMeshGpuBuffers buffers;
  CreateSkeletalBuffersLocked(mesh, buffers);
  SkeletalCache[hash] = buffers;
  return buffers.vao;
}

GLuint CreatureMeshGpuCache::GetOrCreateGltfSkinnedMeshVao(
    const GltfPrimitiveCpu &mesh, size_t &outIndexCount)
{
  const size_t hash = GltfSkinnedMeshKey(mesh);
  {
    std::lock_guard<std::mutex> lock(CacheMutex);
    if (const auto it = GltfSkinnedCache.find(hash); it != GltfSkinnedCache.end())
    {
      outIndexCount = it->second.indexCount;
      return it->second.vao;
    }
  }

  std::lock_guard<std::mutex> lock(CacheMutex);
  if (const auto it = GltfSkinnedCache.find(hash); it != GltfSkinnedCache.end())
  {
    outIndexCount = it->second.indexCount;
    return it->second.vao;
  }
  GltfSkinnedMeshGpuBuffers buffers;
  CreateGltfSkinnedBuffersLocked(mesh, buffers);
  GltfSkinnedCache[hash] = buffers;
  outIndexCount = buffers.indexCount;
  return buffers.vao;
}

void CreatureMeshGpuCache::DestroyAll()
{
  std::lock_guard<std::mutex> lock(CacheMutex);
  for (auto &[key, buffers] : SkeletalCache)
  {
    (void)key;
    if (buffers.ebo)
    {
      glDeleteBuffers(1, &buffers.ebo);
    }
    if (buffers.vbo)
    {
      glDeleteBuffers(1, &buffers.vbo);
    }
    if (buffers.vao)
    {
      glDeleteVertexArrays(1, &buffers.vao);
    }
  }
  SkeletalCache.clear();

  for (auto &[key, buffers] : GltfSkinnedCache)
  {
    (void)key;
    if (buffers.ebo)
    {
      glDeleteBuffers(1, &buffers.ebo);
    }
    if (buffers.vbo)
    {
      glDeleteBuffers(1, &buffers.vbo);
    }
    if (buffers.vao)
    {
      glDeleteVertexArrays(1, &buffers.vao);
    }
  }
  GltfSkinnedCache.clear();
}

void CreatureMeshGpuCache::CreateSkeletalBuffersLocked(
    const BoneSkeletonCubeMeshCpu &mesh, SkeletalMeshGpuBuffers &outBuffers)
{
  SkeletalMeshGpuBuffers &buffers = outBuffers;
  glGenVertexArrays(1, &buffers.vao);
  glGenBuffers(1, &buffers.vbo);
  glGenBuffers(1, &buffers.ebo);
  glBindVertexArray(buffers.vao);
  glBindBuffer(GL_ARRAY_BUFFER, buffers.vbo);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(mesh.interleavedPosUv.size() * sizeof(float)),
      mesh.interleavedPosUv.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers.ebo);
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned int)),
      mesh.indices.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
}

void CreatureMeshGpuCache::CreateGltfSkinnedBuffersLocked(
    const GltfPrimitiveCpu &mesh, GltfSkinnedMeshGpuBuffers &outBuffers)
{
  GltfSkinnedMeshGpuBuffers &buffers = outBuffers;
  const size_t vertCount = mesh.mesh.interleavedPosUv.size() / 5;
  struct SkinnedVertex
  {
    float pos[3];
    float uv[2];
    uint8_t joints[4];
    float weights[4];
  };
  std::vector<SkinnedVertex> vertices(vertCount);
  for (size_t i = 0; i < vertCount; ++i)
  {
    vertices[i].pos[0] = mesh.mesh.interleavedPosUv[i * 5 + 0];
    vertices[i].pos[1] = mesh.mesh.interleavedPosUv[i * 5 + 1];
    vertices[i].pos[2] = mesh.mesh.interleavedPosUv[i * 5 + 2];
    vertices[i].uv[0] = mesh.mesh.interleavedPosUv[i * 5 + 3];
    vertices[i].uv[1] = mesh.mesh.interleavedPosUv[i * 5 + 4];
    for (int j = 0; j < 4; ++j)
    {
      vertices[i].joints[j] = (i * 4 + j < mesh.jointIndices.size())
                                  ? mesh.jointIndices[i * 4 + j]
                                  : 0;
      vertices[i].weights[j] = (i * 4 + j < mesh.jointWeights.size())
                                   ? mesh.jointWeights[i * 4 + j]
                                   : 0.f;
    }
  }

  glGenVertexArrays(1, &buffers.vao);
  glGenBuffers(1, &buffers.vbo);
  glGenBuffers(1, &buffers.ebo);
  glBindVertexArray(buffers.vao);
  glBindBuffer(GL_ARRAY_BUFFER, buffers.vbo);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(SkinnedVertex)),
               vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers.ebo);
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(mesh.mesh.indices.size() * sizeof(unsigned int)),
      mesh.mesh.indices.data(), GL_STATIC_DRAW);
  constexpr GLsizei stride = static_cast<GLsizei>(sizeof(SkinnedVertex));
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, stride,
                         (void *)(5 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                        (void *)(5 * sizeof(float) + 4 * sizeof(uint8_t)));
  glEnableVertexAttribArray(3);
  glBindVertexArray(0);
  buffers.indexCount = mesh.mesh.indices.size();
}

} // namespace cutum
