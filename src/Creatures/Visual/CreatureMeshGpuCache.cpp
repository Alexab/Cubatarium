#include "Creatures/Visual/CreatureMeshGpuCache.h"

#include "Render/GlIncludes.h"
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace cutum
{

CreatureMeshGpuCache &CreatureMeshGpuCache::Instance()
{
  static CreatureMeshGpuCache cache;
  return cache;
}

GLuint CreatureMeshGpuCache::GetOrCreateSkeletalMeshVao(
    const BoneSkeletonCubeMeshCpu &mesh)
{
  size_t hash = 0;
  for (float v : mesh.interleavedPosUv)
  {
    hash = hash * 31 + std::hash<float>{}(v);
  }
  for (unsigned int idx : mesh.indices)
  {
    hash = hash * 31 + idx;
  }
  {
    std::lock_guard<std::mutex> lock(CacheMutex);
    if (const auto it = SkeletalCache.find(hash); it != SkeletalCache.end())
    {
      return it->second.vao;
    }
  }

  SkeletalMeshGpuBuffers buffers;
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
  {
    std::lock_guard<std::mutex> lock(CacheMutex);
    if (const auto it = SkeletalCache.find(hash); it != SkeletalCache.end())
    {
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
      return it->second.vao;
    }
    SkeletalCache[hash] = buffers;
  }
  return buffers.vao;
}

GLuint CreatureMeshGpuCache::GetOrCreateGltfSkinnedMeshVao(
    const GltfPrimitiveCpu &mesh, size_t &outIndexCount)
{
  size_t hash = 0;
  for (float v : mesh.mesh.interleavedPosUv)
  {
    hash = hash * 31 + std::hash<float>{}(v);
  }
  for (uint8_t j : mesh.jointIndices)
  {
    hash = hash * 31 + j;
  }
  for (float w : mesh.jointWeights)
  {
    hash = hash * 31 + std::hash<float>{}(w);
  }
  {
    std::lock_guard<std::mutex> lock(CacheMutex);
    if (const auto it = GltfSkinnedCache.find(hash); it != GltfSkinnedCache.end())
    {
      outIndexCount = it->second.indexCount;
      return it->second.vao;
    }
  }

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

  GltfSkinnedMeshGpuBuffers buffers;
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
  outIndexCount = buffers.indexCount;
  {
    std::lock_guard<std::mutex> lock(CacheMutex);
    if (const auto it = GltfSkinnedCache.find(hash); it != GltfSkinnedCache.end())
    {
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
      outIndexCount = it->second.indexCount;
      return it->second.vao;
    }
    GltfSkinnedCache[hash] = buffers;
  }
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

} // namespace cutum
