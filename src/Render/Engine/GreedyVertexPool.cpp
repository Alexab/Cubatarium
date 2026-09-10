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

  // Opt-in only: per-batch fence waits made wall≈2s (<1 FPS) on full pool rewrite.

  // CUBATARIUM_POOL_SYNC=1 → wait once in Reserve before bump reset.

  const char *env = std::getenv("CUBATARIUM_POOL_SYNC");

  return env && env[0] == '1';

}



enum class PoolFencePoll : uint8_t

{

  Signaled = 0,

  Pending = 1,

  Failed = 2,

};



PoolFencePoll PollPoolFence(void *fence_void, bool blocking, uint64_t timeout_ns,

                            double &wait_ms_acc)

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

#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)

  FlushPendingRetireWithDrawFence();

  for (RetiredSlot &entry : RetiredList)

  {

    FreeList.push_back(entry.slot);

    ++RetiredReclaimedN;

  }

  RetiredList.clear();

  PendingRetireList.clear();

  return;

#else

  if (PoolSyncRequested() && UploadFence != nullptr)

  {

    const PoolFencePoll upload_r =

        PollPoolFence(UploadFence, true, 16'000'000, FenceWaitMs);

    if (upload_r == PoolFencePoll::Signaled)

    {

      DeletePoolFence(UploadFence);

    }

    else

    {

      ++FenceTimeoutN;

      LOG_FIRST_N(WARNING, 4)

          << "[GpuPool] upload fence wait failed/timeout — range not reclaimed "

             "early";

    }

  }



  for (int attempt = 0; attempt < 8; ++attempt)

  {

    FlushPendingRetireWithDrawFence();

    if (RetiredList.empty() && PendingRetireList.empty())

    {

      break;

    }

    if (LastDrawFence != nullptr)

    {

      const PoolFencePoll draw_r =

          PollPoolFence(LastDrawFence, true, 16'000'000, FenceWaitMs);

      if (draw_r != PoolFencePoll::Signaled)

      {

        ++FenceTimeoutN;

        LOG_FIRST_N(WARNING, 4)

            << "[GpuPool] draw fence wait failed/timeout — range not reclaimed "

               "early";

      }

    }

    PollRetiredFences();

  }

#endif

}



void UGreedyVertexPool::PollRetiredFences()

{

#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)

  for (RetiredSlot &entry : RetiredList)

  {

    FreeList.push_back(entry.slot);

    ++RetiredReclaimedN;

  }

  RetiredList.clear();

  return;

#else

  const auto poll_token = [&](uint64_t token) -> PoolFencePoll {

    if (token == 0)

    {

      return PoolFencePoll::Signaled;

    }

    if (LastDrawFenceToken_ == token && LastDrawFence != nullptr)

    {

      return PollPoolFence(LastDrawFence, false, 0, FenceWaitMs);

    }

    return PoolFencePoll::Signaled;

  };



  size_t write = 0;

  for (size_t i = 0; i < RetiredList.size(); ++i)

  {

    RetiredSlot &entry = RetiredList[i];

    const PoolFencePoll r = poll_token(entry.drawFenceToken);

    if (r == PoolFencePoll::Signaled)

    {

      FreeList.push_back(entry.slot);

      ++RetiredReclaimedN;

      continue;

    }

    if (r == PoolFencePoll::Failed)

    {

      ++FenceTimeoutN;

    }

    if (write != i)

    {

      RetiredList[write] = entry;

    }

    ++write;

  }

  RetiredList.resize(write);

#endif

}



void UGreedyVertexPool::BeginUploadFrame()

{

  FrameUnsyncUploads = 0;

  ActiveDrawFenceToken_ = 0;

  PollRetiredFences();

}



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

  WaitUntilRetireQueuesDrained();

  PollRetiredFences();

  if (!RetiredList.empty() || !PendingRetireList.empty())

  {

    LOG_FIRST_N(WARNING, 4)

        << "[GpuPool] Reserve skipped bump reset — retired/pending remain";

    return EnsureCapacity(vertex_bytes, index_bytes);

  }

  ++ReserveBumpN;

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

  PollRetiredFences();

  GreedyGpuPoolFreeSlot slot;

  slot.vertexByteOffset = alloc.vertexByteOffset;

  slot.indexByteOffset = alloc.indexByteOffset;

  slot.vertexBytes = alloc.vertexCount * sizeof(GreedyMeshVertex);

  slot.indexBytes = alloc.indexCount * sizeof(uint32_t);

#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)

  FreeList.push_back(slot);

  return;

#else

  if (ActiveDrawFenceToken_ != 0)

  {

    RetiredSlot retired;

    retired.slot = slot;

    retired.drawFenceToken = ActiveDrawFenceToken_;

    RetiredList.push_back(retired);

    return;

  }

  PendingRetireList.push_back(slot);

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

        MaxUnsyncUploadsPerFrame > 0 &&

        FrameUnsyncUploads < MaxUnsyncUploadsPerFrame;

    if (allow_unsync)

    {

      ++UnsyncUploads;

      ++FrameUnsyncUploads;

    }

    void *mapped = allow_unsync

                       ? glMapBufferRange(

                             kArrayBuffer, static_cast<GLintptr>(alloc.vertexByteOffset),

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

    const bool allow_unsync =

        MaxUnsyncUploadsPerFrame > 0 &&

        FrameUnsyncUploads < MaxUnsyncUploadsPerFrame;

    if (allow_unsync)

    {

      ++UnsyncUploads;

      ++FrameUnsyncUploads;

    }

    void *mapped = allow_unsync

                       ? glMapBufferRange(

                             kElementArrayBuffer,

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



void UGreedyVertexPool::SignalDrawComplete()

{

#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)

  DeletePoolFence(LastDrawFence);

  LastDrawFence = InsertDrawFence();

  LastDrawFenceToken_ = NextFenceToken_++;

  ActiveDrawFenceToken_ = LastDrawFenceToken_;

  for (const GreedyGpuPoolFreeSlot &slot : PendingRetireList)

  {

    RetiredSlot retired;

    retired.slot = slot;

    retired.drawFenceToken = LastDrawFenceToken_;

    RetiredList.push_back(retired);

  }

  PendingRetireList.clear();

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

  PollRetiredFences();

  VertexUsedBytes = 0;

  IndexUsedBytes = 0;

  FreeList.clear();

}



void UGreedyVertexPool::Destroy()

{

  DeletePoolFence(UploadFence);

  DeletePoolFence(LastDrawFence);

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

