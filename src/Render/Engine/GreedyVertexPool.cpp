#include "Render/Engine/GreedyVertexPool.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
namespace cutum
{
namespace
{
constexpr unsigned int kArrayBuffer = GL_ARRAY_BUFFER;
constexpr unsigned int kElementArrayBuffer = GL_ELEMENT_ARRAY_BUFFER;
bool PoolSyncRequested()
{
  // Opt-in only: per-batch fence waits made wall≈2s (<1 FPS) on full pool
  // rewrite. CUBATARIUM_POOL_SYNC=1 → wait once in Reserve before bump reset.
  const char *env = std::getenv("CUBATARIUM_POOL_SYNC");
  return env && env[0] == '1';
}
enum class PoolFencePoll : uint8_t
{
  Signaled = 0,
  Pending = 1,
  Failed = 2,
};
PoolFencePoll PollPoolFence(void *fence_void, bool blocking,
                            uint64_t timeout_ns, double &wait_ms_acc)
{
  if (!fence_void)
  {
    return PoolFencePoll::Signaled;
  }
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)blocking;
  (void)timeout_ns;
  (void)wait_ms_acc;
  return PoolFencePoll::Signaled;
#else
  auto *fence = static_cast<GLsync>(fence_void);
  const auto t0 = std::chrono::steady_clock::now();
  const GLbitfield flags =
      blocking ? GL_SYNC_FLUSH_COMMANDS_BIT : static_cast<GLbitfield>(0);
  const GLuint64 timeout = blocking ? timeout_ns : 0;
  const GLenum r = glClientWaitSync(fence, flags, timeout);
  if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED)
  {
    if (blocking)
    {
      wait_ms_acc += std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - t0)
                         .count();
    }
    return PoolFencePoll::Signaled;
  }
  if (r == GL_TIMEOUT_EXPIRED)
  {
    return PoolFencePoll::Pending;
  }
  return PoolFencePoll::Failed;
#endif
}
void DeletePoolFence(void *&fence_void)
{
  if (!fence_void)
  {
    return;
  }
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  glDeleteSync(static_cast<GLsync>(fence_void));
#endif
  fence_void = nullptr;
}
void *InsertDrawFence()
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  return nullptr;
#else
  return glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
#endif
}
} // namespace
void UGreedyVertexPool::FlushPendingRetireWithDrawFence()
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  for (const GreedyGpuPoolFreeSlot &slot : PendingRetireList)
  {
    FreeList.push_back(slot);
  }
  PendingRetireList.clear();
  return;
#else
  if (PendingRetireList.empty())
  {
    return;
  }
  const uint64_t token =
      LastDrawFenceToken_ != 0 ? LastDrawFenceToken_ : ActiveDrawFenceToken_;
  if (token == 0)
  {
    return;
  }
  for (const GreedyGpuPoolFreeSlot &slot : PendingRetireList)
  {
    RetiredSlot retired;
    retired.slot = slot;
    retired.drawFenceToken = token;
    RetiredList.push_back(retired);
  }
  PendingRetireList.clear();
#endif
}
void UGreedyVertexPool::WaitUntilRetireQueuesDrained()
{
  // Nonblocking: no multi-frame driver wait on the render thread.
  FlushPendingRetireWithDrawFence();
  PollRetiredFences();
}
void UGreedyVertexPool::PollRetiredFences()
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  // A later signaled fence proves earlier commands complete in this GL stream.
  for (auto &entry : DrawFences)
  {
    const auto result = entry.second
                            ? PollPoolFence(entry.second, false, 0, FenceWaitMs)
                            : PoolFencePoll::Failed;
    if (result == PoolFencePoll::Signaled)
      CompletedDrawFenceToken_ =
          std::max(CompletedDrawFenceToken_, entry.first);
    else if (result == PoolFencePoll::Failed)
      ++FenceTimeoutN;
  }
  for (auto it = DrawFences.begin(); it != DrawFences.end();)
  {
    if (it->first <= CompletedDrawFenceToken_)
    {
      DeletePoolFence(it->second);
      it = DrawFences.erase(it);
    }
    else
      ++it;
  }
#endif
  size_t write = 0;
  for (size_t i = 0; i < RetiredList.size(); ++i)
  {
    const auto &entry = RetiredList[i];
    if (entry.drawFenceToken <= CompletedDrawFenceToken_)
    {
      FreeList.push_back(entry.slot);
      ++RetiredReclaimedN;
    }
    else
      RetiredList[write++] = entry;
  }
  RetiredList.resize(write);
}
void UGreedyVertexPool::BeginUploadFrame()
{
  FrameUnsyncUploads = 0;
  ActiveDrawFenceToken_ = 0;
  PollRetiredFences();
}
bool UGreedyVertexPool::EnsureCapacity(size_t vertex_bytes, size_t index_bytes)
{
  const size_t want_v = std::max(VertexCapacityBytes, vertex_bytes);
  const size_t want_i = std::max(IndexCapacityBytes, index_bytes);
  if (MaxCapacityBytes > 0 &&
      (want_v > MaxCapacityBytes || want_i > MaxCapacityBytes - want_v))
    return false; // Cap rejection must not alter either live buffer.
  auto grow = [](GLuint &buffer, size_t &capacity, size_t wanted)
  {
    if (wanted <= capacity)
      return true;
    GLuint replacement = 0;
    glGenBuffers(1, &replacement);
    glBindBuffer(GL_COPY_WRITE_BUFFER, replacement);
    glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(wanted), nullptr,
                 GL_DYNAMIC_DRAW);
    GLint64 actual_size = 0;
    glGetBufferParameteri64v(GL_COPY_WRITE_BUFFER, GL_BUFFER_SIZE,
                             &actual_size);
    if (actual_size < 0 || static_cast<size_t>(actual_size) < wanted)
    {
      glDeleteBuffers(1, &replacement);
      glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
      return false;
    }
    if (buffer && capacity)
    {
      glBindBuffer(GL_COPY_READ_BUFFER, buffer);
      glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0,
                          static_cast<GLsizeiptr>(capacity));
      glDeleteBuffers(1, &buffer);
    }
    buffer = replacement;
    capacity = wanted;
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    return true;
  };
  if (want_v > VertexCapacityBytes || want_i > IndexCapacityBytes)
    StorageReadyAfterToken_ = NextFenceToken_;
  return grow(VertexVbo, VertexCapacityBytes, want_v) &&
         grow(IndexEbo, IndexCapacityBytes, want_i);
}
bool UGreedyVertexPool::Reserve(size_t vertex_bytes, size_t index_bytes)
{
  WaitUntilRetireQueuesDrained();
  if (LiveAllocationCount != 0 || !RetiredList.empty() ||
      !PendingRetireList.empty())
    return false;
  if (!EnsureCapacity(vertex_bytes, index_bytes))
    return false;
  ++ReserveBumpN;
  VertexUsedBytes = 0;
  IndexUsedBytes = 0;
  FreeList.clear();
  return true;
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
    return;
  PollRetiredFences();
  GreedyGpuPoolFreeSlot slot;
  slot.vertexByteOffset = alloc.vertexByteOffset;
  slot.indexByteOffset = alloc.indexByteOffset;
  slot.vertexBytes = alloc.vertexCount * sizeof(GreedyMeshVertex);
  slot.indexBytes = alloc.indexCount * sizeof(uint32_t);
  if (LiveAllocationCount > 0)
    --LiveAllocationCount;
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  FreeList.push_back(slot); // GLES uses synchronized SubData.
#else
  if (LastDrawFenceToken_ <= CompletedDrawFenceToken_)
    FreeList.push_back(slot);
  else
    RetiredList.push_back({slot, LastDrawFenceToken_});
#endif
}
GreedyGpuPoolAllocation
UGreedyVertexPool::Allocate(const GreedyMeshBatch &batch)
{
  PollRetiredFences();
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
    const bool allow_unsync =
        CompletedDrawFenceToken_ >= StorageReadyAfterToken_ &&
        MaxUnsyncUploadsPerFrame > 0 &&
        FrameUnsyncUploads < MaxUnsyncUploadsPerFrame;
    if (allow_unsync)
    {
      ++UnsyncUploads;
      ++FrameUnsyncUploads;
    }
    void *mapped =
        allow_unsync
            ? glMapBufferRange(kArrayBuffer,
                               static_cast<GLintptr>(alloc.vertexByteOffset),
                               static_cast<GLsizeiptr>(vertex_bytes),
                               GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT |
                                   GL_MAP_UNSYNCHRONIZED_BIT)
            : nullptr;
    if (mapped)
    {
      std::memcpy(mapped, batch.vertices.data(), vertex_bytes);
      glUnmapBuffer(kArrayBuffer);
    }
    else
    {
      glBufferSubData(
          kArrayBuffer, static_cast<GLintptr>(alloc.vertexByteOffset),
          static_cast<GLsizeiptr>(vertex_bytes), batch.vertices.data());
    }
  }
#else
  glBufferSubData(kArrayBuffer, static_cast<GLintptr>(alloc.vertexByteOffset),
                  static_cast<GLsizeiptr>(vertex_bytes), batch.vertices.data());
#endif
  glBindBuffer(kElementArrayBuffer, IndexEbo);
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  {
    const bool allow_unsync =
        CompletedDrawFenceToken_ >= StorageReadyAfterToken_ &&
        MaxUnsyncUploadsPerFrame > 0 &&
        FrameUnsyncUploads < MaxUnsyncUploadsPerFrame;
    if (allow_unsync)
    {
      ++UnsyncUploads;
      ++FrameUnsyncUploads;
    }
    void *mapped =
        allow_unsync
            ? glMapBufferRange(kElementArrayBuffer,
                               static_cast<GLintptr>(alloc.indexByteOffset),
                               static_cast<GLsizeiptr>(index_bytes),
                               GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT |
                                   GL_MAP_UNSYNCHRONIZED_BIT)
            : nullptr;
    if (mapped)
    {
      std::memcpy(mapped, batch.indices.data(), index_bytes);
      glUnmapBuffer(kElementArrayBuffer);
    }
    else
    {
      glBufferSubData(
          kElementArrayBuffer, static_cast<GLintptr>(alloc.indexByteOffset),
          static_cast<GLsizeiptr>(index_bytes), batch.indices.data());
    }
  }
#else
  glBufferSubData(kElementArrayBuffer,
                  static_cast<GLintptr>(alloc.indexByteOffset),
                  static_cast<GLsizeiptr>(index_bytes), batch.indices.data());
#endif
  glBindBuffer(kArrayBuffer, 0);
  glBindBuffer(kElementArrayBuffer, 0);
  ++LiveAllocationCount;
  return alloc;
}
void UGreedyVertexPool::SignalDrawComplete()
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  PollRetiredFences();
  LastDrawFenceToken_ = NextFenceToken_++;
  DrawFences.emplace(LastDrawFenceToken_, InsertDrawFence());
#endif
}
void UGreedyVertexPool::SignalUploadComplete()
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (!PoolSyncRequested())
  {
    return;
  }
  DeletePoolFence(UploadFence);
  UploadFence = InsertDrawFence();
#endif
}
void UGreedyVertexPool::Reset()
{
  (void)Reserve(VertexCapacityBytes, IndexCapacityBytes);
}
void UGreedyVertexPool::Destroy()
{
  DeletePoolFence(UploadFence);
  for (auto &entry : DrawFences)
    DeletePoolFence(entry.second);
  DrawFences.clear();
  CompletedDrawFenceToken_ = 0;
  LiveAllocationCount = 0;
  RetiredList.clear();
  PendingRetireList.clear();
  LastDrawFenceToken_ = 0;
  ActiveDrawFenceToken_ = 0;
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
