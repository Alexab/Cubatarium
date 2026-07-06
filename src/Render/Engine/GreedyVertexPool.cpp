#include "Render/Engine/GreedyVertexPool.h"
#include "Render/GlIncludes.h"

namespace cutum
{

namespace
{

constexpr unsigned int kArrayBuffer = GL_ARRAY_BUFFER;
constexpr unsigned int kElementArrayBuffer = GL_ELEMENT_ARRAY_BUFFER;

} // namespace

void UGreedyVertexPool::EnsureCapacity(size_t vertex_bytes, size_t index_bytes)
{
  if (VertexVbo == 0)
  {
    glGenBuffers(1, &VertexVbo);
  }
  if (IndexEbo == 0)
  {
    glGenBuffers(1, &IndexEbo);
  }

  if (vertex_bytes > VertexCapacityBytes)
  {
    glBindBuffer(kArrayBuffer, VertexVbo);
    glBufferData(kArrayBuffer, static_cast<GLsizeiptr>(vertex_bytes), nullptr,
                 GL_DYNAMIC_DRAW);
    VertexCapacityBytes = vertex_bytes;
  }
  if (index_bytes > IndexCapacityBytes)
  {
    glBindBuffer(kElementArrayBuffer, IndexEbo);
    glBufferData(kElementArrayBuffer, static_cast<GLsizeiptr>(index_bytes),
                 nullptr, GL_DYNAMIC_DRAW);
    IndexCapacityBytes = index_bytes;
  }
  glBindBuffer(kArrayBuffer, 0);
  glBindBuffer(kElementArrayBuffer, 0);
}

void UGreedyVertexPool::Reserve(size_t vertex_bytes, size_t index_bytes)
{
  EnsureCapacity(vertex_bytes, index_bytes);
  VertexUsedBytes = 0;
  IndexUsedBytes = 0;
}

GreedyGpuPoolAllocation
UGreedyVertexPool::Allocate(const GreedyMeshBatch &batch)
{
  GreedyGpuPoolAllocation alloc;
  alloc.vertexCount = batch.vertices.size();
  alloc.indexCount = batch.indices.size();
  alloc.indexCountGl = static_cast<GLsizei>(batch.indices.size());
  if (alloc.vertexCount == 0 || alloc.indexCount == 0)
  {
    return alloc;
  }

  const size_t vertex_bytes = alloc.vertexCount * sizeof(GreedyMeshVertex);
  const size_t index_bytes = alloc.indexCount * sizeof(uint32_t);
  const size_t needed_vertex = VertexUsedBytes + vertex_bytes;
  const size_t needed_index = IndexUsedBytes + index_bytes;
  if (needed_vertex > VertexCapacityBytes || needed_index > IndexCapacityBytes)
  {
    EnsureCapacity(needed_vertex, needed_index);
  }

  alloc.vertexByteOffset = VertexUsedBytes;
  alloc.indexByteOffset = IndexUsedBytes;

  glBindBuffer(kArrayBuffer, VertexVbo);
  glBufferSubData(kArrayBuffer, static_cast<GLintptr>(alloc.vertexByteOffset),
                  static_cast<GLsizeiptr>(vertex_bytes), batch.vertices.data());
  glBindBuffer(kElementArrayBuffer, IndexEbo);
  glBufferSubData(kElementArrayBuffer,
                  static_cast<GLintptr>(alloc.indexByteOffset),
                  static_cast<GLsizeiptr>(index_bytes), batch.indices.data());
  glBindBuffer(kArrayBuffer, 0);
  glBindBuffer(kElementArrayBuffer, 0);

  VertexUsedBytes += vertex_bytes;
  IndexUsedBytes += index_bytes;
  return alloc;
}

void UGreedyVertexPool::Reset()
{
  VertexUsedBytes = 0;
  IndexUsedBytes = 0;
}

void UGreedyVertexPool::Destroy()
{
  if (IndexEbo != 0)
  {
    glDeleteBuffers(1, &IndexEbo);
    IndexEbo = 0;
  }
  if (VertexVbo != 0)
  {
    glDeleteBuffers(1, &VertexVbo);
    VertexVbo = 0;
  }
  VertexCapacityBytes = 0;
  IndexCapacityBytes = 0;
  VertexUsedBytes = 0;
  IndexUsedBytes = 0;
}

} // namespace cutum
