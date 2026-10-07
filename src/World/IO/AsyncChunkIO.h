#pragma once

#include "Core/Jobs/JobThreadBudget.h"
#include "Core/Jobs/JobThreadPool.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/IO/ChunkStorageTypes.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
class UChunkStorageService;

struct AsyncChunkLoadResult
{
  glm::ivec3 coord;
  ChunkGenerationToken token;
  std::shared_ptr<std::atomic<bool>> cancellation;
  std::vector<uint8_t> payload;
  UChunkBuffer decodedBuffer;
  ChunkDiskFormat format{ChunkDiskFormat::Absent};
  bool success{false};
  std::chrono::steady_clock::time_point submittedAt{};
  std::chrono::steady_clock::time_point workerStartedAt{};
  std::chrono::steady_clock::time_point workerFinishedAt{};
  double formatDetectMs{0.0};
  double fileOpenMs{0.0};
  double fileReadMs{0.0};
  double deserializeMs{0.0};
};

struct AsyncChunkSaveRequest
{
  glm::ivec3 coord;
  glm::ivec3 groundCoord;
  std::string filePath;
  ChunkDiskFormat format{ChunkDiskFormat::Binary};
  bool success{false};
  std::string error;
};

struct AsyncColumnLightFlagsSaveResult
{
  std::string worldFolder;
  uint64_t revision{0};
  bool success{false};
  std::string error;
};

class UAsyncChunkIO
{
private:
  static std::size_t ChunkIoWorkerBudget()
  {
    return ComputeWorkerThreadCount(JobPoolKind::ChunkIo);
  }

  static std::size_t LoadWorkerBudget()
  {
    return ChunkIoWorkerBudget();
  }

  static std::unique_ptr<UJobThreadPool> CreateBackgroundIoPool()
  {
    const std::size_t load_workers = ChunkIoWorkerBudget();
    const std::size_t hardware_workers = std::thread::hardware_concurrency();
    // Preserve the full load budget and add one save/index worker only when
    // at least one logical processor remains available for the rest of the
    // application and operating system.
    if (hardware_workers <= load_workers + 1)
    {
      return {};
    }
    return std::make_unique<UJobThreadPool>(1, "ChunkIoBackground");
  }

public:
  UAsyncChunkIO()
      : LoadPool(LoadWorkerBudget(), "ChunkIoLoad"),
        BackgroundIoPool(CreateBackgroundIoPool())
  {
  }

  void RequestLoad(glm::ivec3 coord, UChunkStorageService &storage,
                   UBlockRegistry &registry,
                   const std::string &worldFolder, ChunkGenerationToken token,
                   std::shared_ptr<std::atomic<bool>> cancellation);
  void RequestDiskIndexWarmup(UChunkStorageService &storage,
                              const std::string &worldFolder);
  void RequestSave(glm::ivec3 coord, UChunkStorageService &storage,
                   const std::string &worldFolder, const UBlockWorld &world,
                   UBlockRegistry &registry, ChunkGenerationToken token);
  void RequestSaveColumnLightFlags(
      std::string worldFolder, uint64_t revision,
      std::vector<glm::ivec2> completeColumns);

  std::vector<AsyncChunkLoadResult> DrainLoads();
  std::vector<AsyncChunkLoadResult> DrainLoadsUpTo(std::size_t max_count);
  void RequeueLoads(std::vector<AsyncChunkLoadResult> &&loads)
  {
    CompletedLoads.PushRange(std::move(loads));
  }
  template <typename Compare>
  std::vector<AsyncChunkLoadResult>
  DrainLoadsBestUpTo(std::size_t max_count, Compare &&compare)
  {
    return CompletedLoads.DrainBestUpTo(max_count, compare);
  }
  template <typename KeyFn>
  std::vector<AsyncChunkLoadResult>
  DrainLoadsBestByKeyUpTo(std::size_t max_count, KeyFn &&key_fn,
                          std::size_t *available_count = nullptr)
  {
    return CompletedLoads.DrainBestByKeyUpTo(max_count, key_fn,
                                             available_count);
  }
  std::vector<AsyncChunkSaveRequest> DrainSaves();
  std::vector<AsyncColumnLightFlagsSaveResult> DrainColumnLightFlagsSaves();
  bool WaitForColumnLightFlagsSaveIdleFor(std::chrono::milliseconds timeout);
  void WaitForColumnLightFlagsSaveIdle();
  bool CompletedColumnLightFlagsSavesEmpty() const;
  void WaitIdle();
  bool WaitIdleFor(std::chrono::milliseconds timeout);
  void CancelPending();
  void NoteLoadCancellation();
  std::size_t DiscardCancelledLoads();
  bool CompletedLoadsEmpty() const;
  bool CompletedSavesEmpty() const;
  std::size_t GetPendingJobCount() const
  {
    return LoadPool.GetPendingJobCount() +
           (BackgroundIoPool ? BackgroundIoPool->GetPendingJobCount() : 0);
  }
  std::size_t GetActiveJobCount() const
  {
    return LoadPool.GetActiveJobCount() +
           (BackgroundIoPool ? BackgroundIoPool->GetActiveJobCount() : 0);
  }
  std::size_t GetWorkerCount() const
  {
    return LoadPool.GetWorkerCount() +
           (BackgroundIoPool ? BackgroundIoPool->GetWorkerCount() : 0);
  }
  std::size_t GetLoadPendingJobCount() const
  {
    return LoadPool.GetPendingJobCount();
  }
  std::size_t GetBackgroundPendingJobCount() const
  {
    return BackgroundIoPool ? BackgroundIoPool->GetPendingJobCount() : 0;
  }
  std::size_t GetLoadActiveJobCount() const
  {
    return LoadPool.GetActiveJobCount();
  }
  std::size_t GetBackgroundActiveJobCount() const
  {
    return BackgroundIoPool ? BackgroundIoPool->GetActiveJobCount() : 0;
  }
  JobThreadPoolSnapshot GetLoadPoolSnapshot() const
  {
    return LoadPool.GetSnapshot();
  }
  JobThreadPoolSnapshot GetBackgroundPoolSnapshot() const
  {
    return BackgroundIoPool ? BackgroundIoPool->GetSnapshot()
                            : JobThreadPoolSnapshot{};
  }
  std::size_t GetLoadWorkerCount() const { return LoadPool.GetWorkerCount(); }
  std::size_t GetBackgroundWorkerCount() const
  {
    return BackgroundIoPool ? BackgroundIoPool->GetWorkerCount() : 0;
  }
  std::size_t GetLoadResultQueueDepth() const { return CompletedLoads.Size(); }
  std::size_t GetSaveResultQueueDepth() const { return CompletedSaves.Size(); }

private:
  void EnqueueBackgroundIo(std::function<void()> job)
  {
    if (BackgroundIoPool)
    {
      BackgroundIoPool->Enqueue(std::move(job));
    }
    else
    {
      LoadPool.Enqueue(std::move(job));
    }
  }

  // Queues and task-visible state must outlive all worker pools (reverse
  // declaration order controls destruction).
  UCompletedJobQueue<AsyncChunkLoadResult> CompletedLoads;
  UCompletedJobQueue<AsyncChunkSaveRequest> CompletedSaves;
  UCompletedJobQueue<AsyncColumnLightFlagsSaveResult>
      CompletedColumnLightFlagsSaves;
  std::atomic<bool> CancelledLoadSweepPending{false};
  std::unordered_set<std::string> DiskIndexWarmupFolders;
  // Reads retain their full configured budget; writes/index warmup use an
  // extra background worker when the machine has spare logical capacity.
  UJobThreadPool LoadPool;
  std::unique_ptr<UJobThreadPool> BackgroundIoPool;
  // Light-completion metadata is rare and coalesced by WorldPersistence. Keep
  // it separate from terrain saves and cancellable terrain reads.
  UJobThreadPool ColumnLightFlagsPool{1, "ColumnLightFlagsSave"};
};

UChunkBuffer ParseChunkJsonToBuffer(const std::string &jsonText,
                                    glm::ivec3 chunkCoord,
                                    UBlockRegistry &registry);

std::string SerializeChunkToJson(glm::ivec3 chunkCoord, const UChunk &chunk,
                                 UBlockRegistry &registry);

} // namespace cutum
