#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include "World/IO/IoCodecStorage.h"
#include <array>

namespace cutum
{
struct AllocatorSourcePhase;
enum class AsyncIoJobKind : size_t { Load, Save, Receipts, Flags, Warmup, Cleanup, Count };
enum class AsyncIoWorkerStage { Entered, ReadBytes, Decoded, Encoded, BeforePublish, Finished, Cancelled, Codec };
enum class AsyncIoCreditKind : size_t { Load, Write, Flags, Warmup, Count };
inline constexpr size_t kAsyncIoCreditKinds = static_cast<size_t>(AsyncIoCreditKind::Count);

struct AsyncChunkLoadResult;
struct AsyncChunkSaveRequest;
struct AsyncColumnLightFlagsSaveResult;

// Borrowed vectors on the submitting thread. The caller supplies the live
// delivery buffers, including empty vectors whose capacity is still retained.
struct AsyncIoReturnedBatches
{
  const std::vector<AsyncChunkLoadResult> *loads{};
  const std::vector<AsyncChunkSaveRequest> *saves{};
  const std::vector<AsyncColumnLightFlagsSaveResult> *flags{};
};

struct AsyncIoBorrowedRoot
{
  uintptr_t object{}, lifetime{};
  size_t references{};
  bool mixed{};
};

struct AsyncIoRootBinding
{
  uintptr_t io{}, storage{}, registry{}, registry_lifetime{};
  uint64_t epoch{};
  IoDiskIndexStorage disk_index;
};

struct AsyncIoStorage
{
  uintptr_t io{};
  uint64_t epoch{};
  std::array<JobThreadPoolSnapshot, 3> pools{};
  std::array<size_t, 3> pool_queue_bytes{}, completed{}, completed_slots_bytes{};
  std::array<size_t, 3> returned{}, returned_slots_bytes{};
  std::array<size_t, static_cast<size_t>(AsyncIoJobKind::Count)> queued{};
  std::array<uintptr_t, kAsyncIoCreditKinds> credit_counters{};
  std::array<size_t, kAsyncIoCreditKinds> admitted_slots{};
  size_t callable_bytes{}, queued_backing{}, completed_backing{}, shared_bytes{};
  size_t returned_backing{};
  size_t shared_blocks{}, shared_aliases{}, unknown_jobs{}, active{};
  size_t warmup_keys{}, warmup_requested{};
  AsyncIoBorrowedRoot storage_root, registry_root;
  bool supported{}, retained_supported{}, overflow{}, scratch_exhausted{};
};

// Scratch holds identity tokens only. The caller supplies it and keeps the
// I/O owner alive on its submitting thread throughout the synchronous query.
struct AsyncIoStorageIdentity
{
  uintptr_t allocation{};
  size_t bytes{};
  size_t backing{};
  uintptr_t credit_counter{};
  size_t credit_slots{};
};

struct AsyncIoWorkerSample
{
  uintptr_t job{};
  std::thread::id thread;
  AsyncIoJobKind kind{};
  AsyncIoWorkerStage stage{};
  uint64_t key{};
  AsyncIoStorage sources;
  std::array<AsyncIoStorageIdentity, 32> shared{};
  size_t input_bytes{}, decoded_bytes{}, encoded_bytes{}, path_bytes{};
  IoCodecStorage codec;
  IoDiskIndexStorage disk_index;
};

// Synchronous observation from the executing worker. The sink may park the
// worker while the owner reads a coherent phase. The submitting thread keeps
// this borrowed hook live and unchanged until all related jobs retire.
struct AsyncIoWorkerProbe
{
  const AllocatorSourcePhase &phase;
  void *context;
  void (*sample)(void *, const AsyncIoWorkerSample &) noexcept;
};

struct AsyncIoExecutionStorage
{
  size_t participants{}, callable_bytes{}, captured_backing{}, local_backing{};
  size_t queue_slots{}, completion_slots{}, returned_slots{}, returned_backing{}, requested{};
  size_t shared_bytes{}, shared_blocks{}, shared_aliases{};
  size_t storage_references{}, registry_references{}, disk_index_requested{};
  std::array<size_t, kAsyncIoCreditKinds> admitted_slots{};
  bool supported{}, overflow{};
};

// The caller holds participating workers at their samples until the join and
// closes the phase before allowing them to continue. The resulting totals
// cover explicit job storage, supplied delivery buffers and the disk index.
// The world roots and delivery buffers stay live and stable throughout the
// phase. Credit matching detects missing unique credits. The caller supplies
// all live result vectors, including batches that share a write credit.
AsyncIoExecutionStorage JoinAsyncIoWorkers(
    const AllocatorSourcePhase &phase, const AsyncIoRootBinding &roots,
    const AsyncIoStorage &queues,
    const AsyncIoStorageIdentity *queue_shared, size_t queue_shared_count,
    const AsyncIoWorkerSample *const *workers, size_t worker_count,
    AsyncIoStorageIdentity *scratch, size_t scratch_count) noexcept;
} // namespace cutum
