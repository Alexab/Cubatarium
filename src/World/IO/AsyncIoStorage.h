#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include <array>

namespace cutum
{
enum class AsyncIoJobKind : size_t { Load, Save, Receipts, Flags, Warmup, Cleanup, Count };

struct AsyncIoStorage
{
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
};
} // namespace cutum
