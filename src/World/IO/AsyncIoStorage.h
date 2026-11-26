#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include <array>

namespace cutum
{
struct AllocatorSourcePhase;
enum class AsyncIoJobKind : size_t { Load, Save, Receipts, Flags, Warmup, Cleanup, Count };
enum class AsyncIoWorkerStage { Entered, ReadBytes, Decoded, Encoded, BeforePublish, Finished, Cancelled };

struct AsyncIoStorage
{
  uintptr_t io{};
  uint64_t epoch{};
  std::array<JobThreadPoolSnapshot, 3> pools{};
  std::array<size_t, 3> pool_queue_bytes{}, completed{}, completed_slots_bytes{};
  std::array<size_t, static_cast<size_t>(AsyncIoJobKind::Count)> queued{};
  size_t callable_bytes{}, queued_backing{}, completed_backing{}, shared_bytes{};
  size_t shared_blocks{}, shared_aliases{}, unknown_jobs{}, active{};
  bool supported{}, retained_supported{}, overflow{}, scratch_exhausted{};
};

// Scratch holds identity tokens only. The caller supplies it and keeps the
// I/O owner alive on its submitting thread throughout the synchronous query.
struct AsyncIoStorageIdentity
{
  uintptr_t allocation{};
  size_t bytes{};
  size_t backing{};
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
  size_t shared_bytes{}, shared_blocks{}, shared_aliases{};
  bool supported{}, overflow{};
};

// The caller holds participating workers at their samples until the join and
// closes the phase before allowing them to continue. The resulting totals
// cover explicit job storage, not stacks, codec-private scratch or other owners.
AsyncIoExecutionStorage JoinAsyncIoWorkers(
    const AllocatorSourcePhase &phase, const AsyncIoStorage &queues,
    const AsyncIoStorageIdentity *queue_shared, size_t queue_shared_count,
    const AsyncIoWorkerSample *const *workers, size_t worker_count,
    AsyncIoStorageIdentity *scratch, size_t scratch_count) noexcept;
} // namespace cutum
