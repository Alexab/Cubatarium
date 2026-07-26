#include "Render/Engine/GreedyVertexPool.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"
#include <algorithm>
#include <cstring>

namespace cutum
{

namespace
{

constexpr unsigned int kArrayBuffer = GL_ARRAY_BUFFER;
constexpr unsigned int kElementArrayBuffer = GL_ELEMENT_ARRAY_BUFFER;

} // namespace

bool UGreedyVertexPool::EnsureCapacity(size_t vertex_bytes, size_t index_bytes)
{
  if (VertexVbo == 0)
  {
    glGenBuffers(1, &VertexVbo);
  }
  if (IndexEbo == 0)
  {
    glGenBuffers(1, &IndexEbo);
  }

  size_t want_v = vertex_bytes;
  size_t want_i = index_bytes;
  bool clamped = false;
  if (MaxCapacityBytes > 0)
  {
    const size_t current = (std::max)(VertexCapacityBytes, want_v) +
                           (std::max)(IndexCapacityBytes, want_i);
    if (current > MaxCapacityBytes)
    {
      const size_t room_v = MaxCapacityBytes > IndexCapacityBytes
                                ? MaxCapacityBytes - IndexCapacityBytes
                                : 0;
      const size_t room_i = MaxCapacityBytes > VertexCapacityBytes
                                ? MaxCapacityBytes - VertexCapacityBytes
                                : 0;
      if (want_v > room_v)
      {
        want_v = (std::max)(VertexCapacityBytes, room_v);
        clamped = true;
      }
      if (want_i > room_i)
      {
        want_i = (std::max)(IndexCapacityBytes, room_i);
        clamped = true;
      }
      if (clamped)
      {
        LOG_FIRST_N(WARNING, 8)
            << "[GpuPool] EnsureCapacity clamped to MaxMb "
            << (MaxCapacityBytes / (1024 * 1024));
      }
    }
  }

  if (want_v > VertexCapacityBytes)
  {
    glBindBuffer(kArrayBuffer, VertexVbo);
    glBufferData(kArrayBuffer, static_cast<GLsizeiptr>(want_v), nullptr,
                 GL_DYNAMIC_DRAW);
    VertexCapacityBytes = want_v;
  }
  if (want_i > IndexCapacityBytes)
  {
    glBindBuffer(kElementArrayBuffer, IndexEbo);
    glBufferData(kElementArrayBuffer, static_cast<GLsizeiptr>(want_i), nullptr,
                 GL_DYNAMIC_DRAW);
    IndexCapacityBytes = want_i;
  }
  glBindBuffer(kArrayBuffer, 0);
  glBindBuffer(kElementArrayBuffer, 0);
  return !clamped && vertex_bytes <= VertexCapacityBytes &&
         index_bytes <= IndexCapacityBytes;
}

bool UGreedyVertexPool::Reserve(size_t vertex_bytes, size_t index_bytes)
{
  const bool ok = EnsureCapacity(vertex_bytes, index_bytes);
  VertexUsedBytes = 0;
  IndexUsedBytes = 0;
  FreeList.clear();
  return ok;
}

bool UGreedyVertexPool::EnsureMinCapacity(size_t vertex_bytes,
                                          size_t index_bytes)
{
  return EnsureCapacity((std::max)(vertex_bytes, VertexCapacityBytes),
                        (std::max)(index_bytes, IndexCapacityBytes));
}

bool UGreedyVertexPool::TryAllocateFromFreeList(size_t vertex_bytes,
                                                size_t index_bytes,
                                                GreedyGpuPoolAllocation &out)
{
  for (size_t i = 0; i < FreeList.size(); ++i)
  {
    GreedyGpuPoolFreeSlot &slot = FreeList[i];
    if (slot.vertexBytes >= vertex_bytes && slot.indexBytes >= index_bytes)
    {
      out.vertexByteOffset = slot.vertexByteOffset;
      out.indexByteOffset = slot.indexByteOffset;
      FreeList.erase(FreeList.begin() + static_cast<std::ptrdiff_t>(i));
      return true;
    }
  }
  return false;
}

void UGreedyVertexPool::Free(const GreedyGpuPoolAllocation &alloc)
{
  if (alloc.vertexCount == 0 || alloc.indexCount == 0)
  {
    return;
  }
  GreedyGpuPoolFreeSlot slot;
  slot.vertexByteOffset = alloc.vertexByteOffset;
  slot.indexByteOffset = alloc.indexByteOffset;
  slot.vertexBytes = alloc.vertexCount * sizeof(GreedyMeshVertex);
  slot.indexBytes = alloc.indexCount * sizeof(uint32_t);
  FreeList.push_back(slot);
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

  if (!TryAllocateFromFreeList(vertex_bytes, index_bytes, alloc))
  {
    const size_t needed_vertex = VertexUsedBytes + vertex_bytes;
    const size_t needed_index = IndexUsedBytes + index_bytes;
    if (needed_vertex > VertexCapacityBytes ||
        needed_index > IndexCapacityBytes)
    {
      if (!EnsureCapacity(needed_vertex, needed_index))
      {
        return GreedyGpuPoolAllocation{};
      }
    }
    alloc.vertexByteOffset = VertexUsedBytes;
    alloc.indexByteOffset = IndexUsedBytes;
    VertexUsedBytes += vertex_bytes;
    IndexUsedBytes += index_bytes;
  }

  glBindBuffer(kArrayBuffer, VertexVbo);
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  {
    void *mapped = glMapBufferRange(
        kArrayBuffer, static_cast<GLintptr>(alloc.vertexByteOffset),
        static_cast<GLsizeiptr>(vertex_bytes),
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT |
            GL_MAP_UNSYNCHRONIZED_BIT);
    if (mapped)
    {
      std::memcpy(mapped, batch.vertices.data(), vertex_bytes);
      glUnmapBuffer(kArrayBuffer);
    }
    else
    {
      glBufferSubData(kArrayBuffer,
                      static_cast<GLintptr>(alloc.vertexByteOffset),
                      static_cast<GLsizeiptr>(vertex_bytes),
                      batch.vertices.data());
    }
  }
#else
  glBufferSubData(kArrayBuffer, static_cast<GLintptr>(alloc.vertexByteOffset),
                  static_cast<GLsizeiptr>(vertex_bytes), batch.vertices.data());
#endif
  glBindBuffer(kElementArrayBuffer, IndexEbo);
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  {
    void *mapped = glMapBufferRange(
        kElementArrayBuffer, static_cast<GLintptr>(alloc.indexByteOffset),
        static_cast<GLsizeiptr>(index_bytes),
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT |
            GL_MAP_UNSYNCHRONIZED_BIT);
    if (mapped)
    {
      std::memcpy(mapped, batch.indices.data(), index_bytes);
      glUnmapBuffer(kElementArrayBuffer);
    }
    else
    {
      glBufferSubData(kElementArrayBuffer,
                      static_cast<GLintptr>(alloc.indexByteOffset),
                      static_cast<GLsizeiptr>(index_bytes),
                      batch.indices.data());
    }
  }
#else
  glBufferSubData(kElementArrayBuffer,
                  static_cast<GLintptr>(alloc.indexByteOffset),
                  static_cast<GLsizeiptr>(index_bytes), batch.indices.data());
#endif
  glBindBuffer(kArrayBuffer, 0);
  glBindBuffer(kElementArrayBuffer, 0);
  return alloc;
}

void UGreedyVertexPool::Reset()
{
  VertexUsedBytes = 0;
  IndexUsedBytes = 0;
  FreeList.clear();
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
  FreeList.clear();
}

} // namespace cutum
