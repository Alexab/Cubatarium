#include "Creatures/Visual/CreatureMeshGpuCache.h"

#include "Core/Memory/StlSourceStorage.h"
#include "Creatures/Visual/CreatureMeshGpuHash.h"
#include "Render/Engine/TerrainGpuBuffers.h"
#include "Render/GlIncludes.h"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace cutum
{
namespace
{

struct SkinnedVertex
{
  float pos[3];
  float uv[2];
  uint8_t joints[4];
  float weights[4];
};

constexpr auto MaxBufferBytes = static_cast<size_t>(std::numeric_limits<GLsizeiptr>::max());

struct RestoreMeshBindings
{
  GLint VertexArray{}, ArrayBuffer{};
  RestoreMeshBindings()
  {
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &VertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &ArrayBuffer);
  }
  ~RestoreMeshBindings()
  {
    glBindVertexArray(GLuint(VertexArray));
    glBindBuffer(GL_ARRAY_BUFFER, GLuint(ArrayBuffer));
  }
};

} // namespace

bool LegacyItemGpuScratch::Draw(const BoneSkeletonCubeMeshCpu &mesh)
{
  constexpr size_t floatsPerVertex = 5;
  const auto &positionsAndUv = mesh.interleavedPosUv;
  const auto &indices = mesh.indices;
  if (positionsAndUv.empty() || positionsAndUv.size() % floatsPerVertex || indices.empty() ||
      positionsAndUv.size() > MaxBufferBytes / sizeof(float) || indices.size() > MaxBufferBytes / sizeof(unsigned) ||
      indices.size() > static_cast<size_t>(std::numeric_limits<GLsizei>::max()))
    return false;

  const bool cold = Vao == 0;
  if (cold)
  {
    glGenVertexArrays(1, &Vao);
    glGenBuffers(1, &Vbo);
    glGenBuffers(1, &Ebo);
    if (!Vao || !Vbo || !Ebo)
    {
      Reset();
      return false;
    }
  }
  const RestoreMeshBindings restore;
  glBindVertexArray(Vao);
  glBindBuffer(GL_ARRAY_BUFFER, Vbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(positionsAndUv.size() * sizeof(float)),
               positionsAndUv.data(), GL_STREAM_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned)),
               indices.data(), GL_STREAM_DRAW);
  if (glGetError() != GL_NO_ERROR)
  {
    if (cold)
      Reset();
    return false;
  }
  if (cold)
  {
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, floatsPerVertex * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, floatsPerVertex * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    if (glGetError() != GL_NO_ERROR)
    {
      Reset();
      return false;
    }
  }
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
  return true;
}

void LegacyItemGpuScratch::Reset() noexcept
{
  if (Vao)
    glDeleteVertexArrays(1, &Vao);
  if (Vbo)
    glDeleteBuffers(1, &Vbo);
  if (Ebo)
    glDeleteBuffers(1, &Ebo);
  Vao = Vbo = Ebo = 0;
}

void LegacyItemGpuScratch::InspectGpuBuffers(TerrainGpuBufferCensus &out, TerrainGpuBufferRole vertices,
                                           TerrainGpuBufferRole indices) const
{
  out.Add(vertices, Vbo);
  out.Add(indices, Ebo);
}

CreatureMeshGpuCache &CreatureMeshGpuCache::Instance()
{
  static CreatureMeshGpuCache cache;
  return cache;
}

GLuint CreatureMeshGpuCache::GetOrCreateSkeletalMeshVao(const BoneSkeletonCubeMeshCpu &mesh)
{
  const auto hash = mesh.GpuHash ? mesh.GpuHash : SkeletalMeshGpuHash(mesh);
  std::lock_guard<std::mutex> lock(CacheMutex);
  if (const auto it = SkeletalCache.find(hash); it != SkeletalCache.end())
    return it->second.vao;
  if (mesh.interleavedPosUv.size() > MaxBufferBytes / sizeof(float) || mesh.interleavedPosUv.size() % 5)
    return 0;
  return UploadBuffers(SkeletalCache, hash, mesh.interleavedPosUv.data(),
      mesh.interleavedPosUv.size() * sizeof(float), mesh.indices, false);
}

GLuint CreatureMeshGpuCache::GetOrCreateGltfSkinnedMeshVao(
    const GltfPrimitiveCpu &mesh, size_t &outIndexCount)
{
  outIndexCount = 0;
  const auto hash = mesh.GpuHash ? mesh.GpuHash : GltfSkinnedMeshGpuHash(mesh);
  std::lock_guard<std::mutex> lock(CacheMutex);
  if (const auto it = GltfSkinnedCache.find(hash); it != GltfSkinnedCache.end())
  {
    outIndexCount = it->second.indexCount;
    return it->second.vao;
  }

  const size_t vertCount = mesh.mesh.interleavedPosUv.size() / 5;
  if (vertCount == 0 || mesh.mesh.interleavedPosUv.size() % 5 || vertCount > MaxBufferBytes / sizeof(SkinnedVertex))
    return 0;
  std::vector<SkinnedVertex> vertices(vertCount);
  for (size_t i = 0; i < vertCount; ++i)
  {
    auto &vertex = vertices[i];
    const auto *source = mesh.mesh.interleavedPosUv.data() + i * 5;
    std::copy_n(source, 3, vertex.pos);
    std::copy_n(source + 3, 2, vertex.uv);
    for (size_t j = 0; j < 4; ++j)
    {
      const size_t component = i * 4 + j;
      vertex.joints[j] = component < mesh.jointIndices.size() ? mesh.jointIndices[component] : 0;
      vertex.weights[j] = component < mesh.jointWeights.size() ? mesh.jointWeights[component] : 0.f;
    }
  }
  const auto vao = UploadBuffers(GltfSkinnedCache, hash, vertices.data(),
      vertices.size() * sizeof(SkinnedVertex), mesh.mesh.indices, true);
  if (vao)
    outIndexCount = mesh.mesh.indices.size();
  return vao;
}

CreatureMeshGpuStorage CreatureMeshGpuCache::InspectOwnedStorage() const
{
  std::lock_guard<std::mutex> lock(CacheMutex);
  StlSourceStorage table, vertex, index;
  for (const auto *entries : {&SkeletalCache, &GltfSkinnedCache})
  {
    table.Hash(*entries);
    for (const auto &[key, buffers] : *entries)
    {
      table.bytes.supported &= buffers.vao && buffers.vbo && buffers.ebo && buffers.vbo != buffers.ebo;
      vertex.Add(buffers.vertexBytes);
      index.Add(buffers.indexCount * sizeof(unsigned int));
    }
  }
  return {sizeof(*this), table.bytes.requested, SkeletalCache.size(), GltfSkinnedCache.size(),
          vertex.bytes.requested, index.bytes.requested,
          table.bytes.supported && vertex.bytes.supported && index.bytes.supported};
}

bool CreatureMeshGpuCache::DrawLegacyEquipment(const BoneSkeletonCubeMeshCpu &mesh)
{
  std::lock_guard<std::mutex> lock(CacheMutex);
  return EquipmentScratch.Draw(mesh);
}

void CreatureMeshGpuCache::InspectGpuBuffers(TerrainGpuBufferCensus &out) const
{
  std::lock_guard<std::mutex> lock(CacheMutex);
  using Role = TerrainGpuBufferRole;
  for (const auto &[key, buffers] : SkeletalCache)
  {
    out.Add(Role::CreatureSkeletalVertices, buffers.vbo);
    out.Add(Role::CreatureSkeletalIndices, buffers.ebo);
  }
  for (const auto &[key, buffers] : GltfSkinnedCache)
  {
    out.Add(Role::CreatureSkinnedVertices, buffers.vbo);
    out.Add(Role::CreatureSkinnedIndices, buffers.ebo);
  }
  EquipmentScratch.InspectGpuBuffers(out, Role::EquipmentScratchVertices, Role::EquipmentScratchIndices);
}

void CreatureMeshGpuCache::DeleteBuffers(const MeshBuffers &buffers) noexcept
{
  if (buffers.vao)
    glDeleteVertexArrays(1, &buffers.vao);
  if (buffers.vbo)
    glDeleteBuffers(1, &buffers.vbo);
  if (buffers.ebo)
    glDeleteBuffers(1, &buffers.ebo);
}

void CreatureMeshGpuCache::DestroyAll()
{
  std::lock_guard<std::mutex> lock(CacheMutex);
  EquipmentScratch.Reset();
  for (auto *entries : {&SkeletalCache, &GltfSkinnedCache})
  {
    for (const auto &[key, buffers] : *entries)
      DeleteBuffers(buffers);
    entries->clear();
  }
}

GLuint CreatureMeshGpuCache::UploadBuffers(MeshTable &table, size_t hash, const void *vertices,
    size_t vertexBytes, const std::vector<unsigned int> &indices, bool skinned)
{
  if (!vertexBytes || indices.empty() || indices.size() > MaxBufferBytes / sizeof(unsigned int) ||
      indices.size() > static_cast<size_t>(std::numeric_limits<GLsizei>::max()))
    return 0;
  const auto indexBytes = indices.size() * sizeof(unsigned int);
  struct Candidate
  {
    MeshBuffers Buffers;
    ~Candidate() { DeleteBuffers(Buffers); }
  } candidate;
  auto &buffers = candidate.Buffers;
  glGenVertexArrays(1, &buffers.vao);
  glGenBuffers(1, &buffers.vbo);
  glGenBuffers(1, &buffers.ebo);
  if (!buffers.vao || !buffers.vbo || !buffers.ebo)
    return 0;
  {
    RestoreMeshBindings restore;
    glBindVertexArray(buffers.vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffers.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertexBytes), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indexBytes), indices.data(), GL_STATIC_DRAW);
    GLint64 verticesReady = 0, indicesReady = 0;
    glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &verticesReady);
    glGetBufferParameteri64v(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &indicesReady);
    if (verticesReady != GLint64(vertexBytes) || indicesReady != GLint64(indexBytes))
      return 0;

    const auto stride = GLsizei(skinned ? sizeof(SkinnedVertex) : 5 * sizeof(float));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    if (skinned)
    {
      glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, stride, reinterpret_cast<void *>(offsetof(SkinnedVertex, joints)));
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(SkinnedVertex, weights)));
      glEnableVertexAttribArray(3);
    }
  }
  buffers.vertexBytes = vertexBytes;
  buffers.indexCount = indices.size();
  const auto [entry, inserted] = table.emplace(hash, buffers);
  if (inserted)
    buffers = {};
  return entry->second.vao;
}

} // namespace cutum
