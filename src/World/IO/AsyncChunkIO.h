#pragma once

#include "Core/Jobs/JobThreadBudget.h"
#include "Core/Jobs/JobThreadPool.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/IO/ChunkStorageTypes.h"
#include "World/IO/AsyncIoStorage.h"
#include "World/IO/ReadyDataAdmission.h"
#include "World/Lighting/PersistedSectionLightTypes.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace cutum
{
struct AsyncIoWorkerLocals;
struct AsyncIoCredit;

class UCore;
class UBlockRegistry;
class UBlockWorld;
class UChunkStorageService;
class UWorldPersistence;

struct AsyncChunkLoadResult
{
  // Declared first so payload storage retires before its admission credit.
  std::shared_ptr<AsyncIoCredit> readCredit;
  glm::ivec3 coord;
  uint64_t demandId{0};
  ChunkGenerationToken token;
  std::shared_ptr<std::atomic<bool>> cancellation;
  std::vector<uint8_t> payload;
  // Keep the large chunk-light array out of the completion queue's inline
  // ring entries. Queue ranking, requeue, and ring growth should move a small
  // handle rather than copying a full UChunkBuffer.
  std::unique_ptr<UChunkBuffer> decodedBuffer;
  std::shared_ptr<const PersistedColumnLight> columnLight;
  ChunkDiskFormat format{ChunkDiskFormat::Absent};
  bool success{false};
  bool injectedInvalidFrame{false};
  std::chrono::steady_clock::time_point submittedAt{};
  std::chrono::steady_clock::time_point workerStartedAt{};
  std::chrono::steady_clock::time_point workerFinishedAt{};
  double formatDetectMs{0.0};
  double fileOpenMs{0.0};
  double fileReadMs{0.0};
  double deserializeMs{0.0};
};

struct AsyncChunkLoadQueuePushMetrics
{
  double mutex_wait_ms{0.0};
  double mutex_held_ms{0.0};
  double mutex_wait_max_ms{0.0};
  double mutex_held_max_ms{0.0};
  uint64_t push_count{0};
};

// Only the explicit native save probe installs this gate. Normal writes do
// not wait; a probe timeout yields an ordinary failed ACK, never a lost task.
struct AsyncSaveWriteProbeGate
{
  std::atomic<bool> entered{false}, released{false};
  std::chrono::milliseconds limit{5000};
};

// Opt-in native lifecycle probe: park one demand after real read/decode,
// before the final cancellation check. Ordinary reads have no gate.
struct AsyncLoadReadProbeGate
{
  uint64_t demandId{0};
  std::atomic<bool> entered{false}, released{false}, timedOut{false};
  std::chrono::milliseconds limit{5000};
};

enum class AsyncSaveOperation : uint8_t { TerrainSlice, LightReceipts, SliceRange, FullColumn };

inline const char *AsyncSaveOperationName(AsyncSaveOperation operation) noexcept
{
  switch (operation)
  {
  case AsyncSaveOperation::LightReceipts: return "column_light_receipts";
  case AsyncSaveOperation::SliceRange: return "slice_range";
  case AsyncSaveOperation::FullColumn: return "full_column";
  default: return "terrain_slice";
  }
}

struct AsyncChunkSaveRequest
{
  std::shared_ptr<AsyncIoCredit> writeCredit;
  uint64_t saveBatchId{0};
  glm::ivec3 coord;
  glm::ivec3 groundCoord;
  std::string worldFolder;
  ChunkDiskFormat format{ChunkDiskFormat::Binary};
  AsyncSaveOperation operation{AsyncSaveOperation::TerrainSlice};
  bool success{false};
  int64_t cleanupSliceCount{0};
  double cleanupMs{0.0};
  std::string error;

  bool IsCleanup() const noexcept { return operation != AsyncSaveOperation::TerrainSlice; }
};

struct AsyncColumnLightFlagsSaveResult
{
  std::shared_ptr<AsyncIoCredit> writeCredit;
  std::string worldFolder;
  uint64_t revision{0};
  bool success{false};
  std::string error;
  double worker_queue_wait_ms{0.0};
  double worker_service_ms{0.0};
};

class UAsyncChunkIO
{
private:
  friend void RunTerrainSaveFailureProbe(UCore &core);
  friend struct AsyncIoStorageProbe;
  friend struct AsyncIoStorageInspector;
  friend class UWorldPersistence;
  std::shared_ptr<AsyncSaveWriteProbeGate> SaveWriteProbeGate;
  std::shared_ptr<AsyncLoadReadProbeGate> LoadReadProbeGate;
  const AsyncIoWorkerProbe *WorkerProbe{nullptr};
  template<class Job>
  void ObserveWorker(const Job &job, AsyncIoJobKind kind, AsyncIoWorkerStage stage,
                     uint64_t key, const AsyncIoWorkerLocals &locals) const noexcept;
  static std::size_t ChunkIoWorkerBudget()
  {
    return ComputeWorkerThreadCount(JobPoolKind::ChunkIo);
  }

  static std::size_t LoadWorkerBudget()
  {
    return ChunkIoWorkerBudget();
  }

  static bool HasBackgroundIoBudget()
  {
    return std::thread::hardware_concurrency() > ChunkIoWorkerBudget() + 1;
  }

  static std::unique_ptr<UJobThreadPool> CreateBackgroundIoPool()
  {
    // Preserve the full load budget and add one save/index worker only when
    // at least one logical processor remains available for the rest of the
    // application and operating system.
    if (!HasBackgroundIoBudget())
    {
      return {};
    }
    return std::make_unique<UJobThreadPool>(1, "ChunkIoBackground");
  }

public:
  AsyncIoStorage InspectRetainedIo(AsyncIoStorageIdentity *scratch,
                                   size_t scratch_count,
                                   const AllocatorSourcePhase *phase = nullptr) const;
  AsyncIoRootBinding InspectBorrowedIoRoots(const AllocatorSourcePhase &phase,
                                           const UChunkStorageService &storage,
                                           const UBlockRegistry &registry) const;
  template<class Inspector> void InspectJobPools(Inspector &out) const {
    out.Pool("io-load",LoadPool,4);
    if(BackgroundIoPool)out.Pool("io-background",*BackgroundIoPool,1);
    out.Pool("io-light-flags",ColumnLightFlagsPool,1);
  }
  UAsyncChunkIO()
      : LoadPool(LoadWorkerBudget(), "ChunkIoLoad"),
        BackgroundIoPool(CreateBackgroundIoPool())
  {
    // A valid result keeps its admission credit through queueing and delivery.
    // Prepare the ring memory before accepting any world I/O.
    CompletedLoads.ReserveStorage(kMaxOutstandingLoads);
    CompletedSaves.ReserveStorage(kMaxOutstandingWrites);
    CompletedColumnLightFlagsSaves.ReserveStorage(kMaxOutstandingFlagSaves);
  }

  static constexpr size_t kMaxOutstandingLoads = 256;
  static constexpr size_t kMaxOutstandingWrites = 256;
  static constexpr size_t kMaxOutstandingFlagSaves = 1;
  size_t GetOutstandingLoadCount() const noexcept
  { return OutstandingLoads->load(std::memory_order_relaxed); }
  size_t GetOutstandingWriteCount() const noexcept
  { return OutstandingWrites->load(std::memory_order_relaxed); }
  size_t GetOutstandingFlagSaveCount() const noexcept
  { return OutstandingFlagSaves->load(std::memory_order_relaxed); }
  bool RequestLoad(glm::ivec3 coord, UChunkStorageService &storage,
                   UBlockRegistry &registry, const std::string &worldFolder,
                   ChunkGenerationToken token,
                   std::shared_ptr<std::atomic<bool>> cancellation,
                   uint64_t demandId, bool includeColumnLight = false,
                   bool injectInvalidFrame = false);
  bool RequestSaveColumnLight(
      glm::ivec3 column, std::shared_ptr<const PersistedColumnLight> receipts,
      UChunkStorageService &storage, const std::string &worldFolder,
      uint64_t saveBatchId = 0);
  bool RequestDiskIndexWarmup(UChunkStorageService &storage,
                             const std::string &worldFolder);
  size_t GetOutstandingIndexWarmupCount() const noexcept
  { return OutstandingIndexWarmups->load(std::memory_order_relaxed); }
  bool RequestSave(glm::ivec3 coord, UChunkStorageService &storage,
                   const std::string &worldFolder, const UBlockWorld &world,
                   UBlockRegistry &registry, ChunkGenerationToken token,
                   uint64_t saveBatchId = 0);
  bool RequestRemoveChunkSlices(glm::ivec3 groundCoord, int firstCy,
                                int lastCy, UChunkStorageService &storage,
                                const std::string &worldFolder,
                                uint64_t saveBatchId = 0);
  bool RequestRemoveTerrainColumn(glm::ivec3 groundCoord, int maxWorldY,
                                  int minWorldY,
                                  UChunkStorageService &storage,
                                  const std::string &worldFolder);
  bool RequestSaveColumnLightFlags(std::string worldFolder, uint64_t revision,
                                   std::vector<glm::ivec2> completeColumns,
                                   std::string lightSignature = {});

  std::vector<AsyncChunkLoadResult> DrainLoads();
  std::vector<AsyncChunkLoadResult> DrainLoadsUpTo(std::size_t max_count);
  void RequeueLoads(std::vector<AsyncChunkLoadResult> &&loads,
                    double *mutex_wait_ms = nullptr,
                    double *mutex_held_ms = nullptr)
  {
    CompletedLoads.PushRange(std::move(loads), mutex_wait_ms, mutex_held_ms);
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
                          std::size_t *available_count = nullptr,
                          double *mutex_wait_ms = nullptr,
                          double *mutex_held_ms = nullptr)
  {
    return CompletedLoads.DrainBestByKeyUpTo(max_count, key_fn,
                                             available_count, mutex_wait_ms,
                                             mutex_held_ms);
  }
  std::vector<AsyncChunkSaveRequest> DrainSaves();
  template <typename Rank, typename Required>
  std::vector<AsyncChunkLoadResult>
  DrainConsumerLoadsUpTo(std::size_t max_count, Rank rank, Required required,
                         bool prefer_consumer, std::size_t *available = nullptr,
                         double *wait_ms = nullptr, double *held_ms = nullptr)
  {
    return DrainReadyDataForConsumers(CompletedLoads, max_count, rank, required,
                                      prefer_consumer, available, wait_ms,
                                      held_ms);
  }
  std::vector<AsyncColumnLightFlagsSaveResult> DrainColumnLightFlagsSaves(
      double *mutex_wait_ms = nullptr, double *mutex_held_ms = nullptr);
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
  bool HasOutstandingJobs() const
  {
    const auto load = LoadPool.GetSnapshot();
    const auto background = BackgroundIoPool ? BackgroundIoPool->GetSnapshot()
                                              : JobThreadPoolSnapshot{};
    const auto flags = ColumnLightFlagsPool.GetSnapshot();
    return load.pending || load.active || background.pending || background.active ||
           flags.pending || flags.active;
  }
  std::size_t GetLoadResultQueueDepth() const { return CompletedLoads.Size(); }
  std::size_t GetSaveResultQueueDepth() const { return CompletedSaves.Size(); }
  AsyncChunkLoadQueuePushMetrics TakeLoadResultPushMetrics()
  {
    constexpr double kNanosecondsToMilliseconds = 1.0e-6;
    AsyncChunkLoadQueuePushMetrics metrics;
    metrics.mutex_wait_ms = static_cast<double>(
                                LoadResultPushWaitNs.exchange(
                                    0, std::memory_order_relaxed)) *
                            kNanosecondsToMilliseconds;
    metrics.mutex_held_ms = static_cast<double>(
                                LoadResultPushHeldNs.exchange(
                                    0, std::memory_order_relaxed)) *
                            kNanosecondsToMilliseconds;
    metrics.mutex_wait_max_ms = static_cast<double>(
                                    LoadResultPushWaitMaxNs.exchange(
                                        0, std::memory_order_relaxed)) *
                                kNanosecondsToMilliseconds;
    metrics.mutex_held_max_ms = static_cast<double>(
                                    LoadResultPushHeldMaxNs.exchange(
                                        0, std::memory_order_relaxed)) *
                                kNanosecondsToMilliseconds;
    metrics.push_count =
        LoadResultPushCount.exchange(0, std::memory_order_relaxed);
    return metrics;
  }

private:
  struct LoadJob;
  struct SaveJob;
  struct SaveReceiptsJob;
  struct SaveFlagsJob;
  struct IndexWarmupJob;
  struct CleanupJob;
  struct RemoveSlices;
  struct RemoveColumn;
  using StorageCleanupJob = std::function<bool(
      UChunkStorageService &, const std::string &, std::string &)>;

  std::shared_ptr<AsyncIoCredit> ReserveTerrainWrites(size_t count);
  std::shared_ptr<AsyncIoCredit> ReserveFlagSave();
  void QueueColumnLightFlagsSave(std::shared_ptr<AsyncIoCredit> credit,
      std::string worldFolder, uint64_t revision, std::vector<glm::ivec2> columns,
      std::string lightSignature);
  void QueueTerrainColumnSave(std::shared_ptr<AsyncIoCredit> credit,
      glm::ivec3 ground, int firstCy, int highestCy, int lastCy,
      UChunkStorageService &storage, const std::string &worldFolder,
      const UBlockWorld &world, std::shared_ptr<const PersistedColumnLight> light,
      uint64_t saveBatchId);
  SaveJob MakeSaveJob(std::shared_ptr<AsyncIoCredit> credit, glm::ivec3 coord,
      const ChunkStorageSettings &settings, const std::string &worldFolder,
      const UBlockWorld &world, uint64_t saveBatchId);
  CleanupJob MakeRemoveSlicesJob(std::shared_ptr<AsyncIoCredit> credit, glm::ivec3 ground,
      int firstCy, int lastCy, UChunkStorageService &storage,
      const std::string &worldFolder, uint64_t saveBatchId);

  static void AccumulateMaximum(std::atomic<uint64_t> &target,
                                uint64_t value)
  {
    uint64_t observed = target.load(std::memory_order_relaxed);
    while (observed < value &&
           !target.compare_exchange_weak(observed, value,
                                         std::memory_order_relaxed,
                                         std::memory_order_relaxed))
    {
    }
  }

  void PushCompletedLoad(AsyncChunkLoadResult &&result)
  {
    double wait_ms = 0.0;
    double held_ms = 0.0;
    CompletedLoads.Push(std::move(result), &wait_ms, &held_ms);
    constexpr double kMillisecondsToNanoseconds = 1.0e6;
    const auto wait_ns = static_cast<uint64_t>(
        (std::max)(0.0, wait_ms) * kMillisecondsToNanoseconds);
    const auto held_ns = static_cast<uint64_t>(
        (std::max)(0.0, held_ms) * kMillisecondsToNanoseconds);
    LoadResultPushWaitNs.fetch_add(wait_ns, std::memory_order_relaxed);
    LoadResultPushHeldNs.fetch_add(held_ns, std::memory_order_relaxed);
    AccumulateMaximum(LoadResultPushWaitMaxNs, wait_ns);
    AccumulateMaximum(LoadResultPushHeldMaxNs, held_ns);
    LoadResultPushCount.fetch_add(1, std::memory_order_relaxed);
  }

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
  std::shared_ptr<std::atomic<size_t>> OutstandingLoads{std::make_shared<std::atomic<size_t>>(0)};
  std::shared_ptr<std::atomic<size_t>> OutstandingWrites{std::make_shared<std::atomic<size_t>>(0)};
  std::shared_ptr<std::atomic<size_t>> OutstandingFlagSaves{std::make_shared<std::atomic<size_t>>(0)};
  std::shared_ptr<std::atomic<size_t>> OutstandingIndexWarmups{std::make_shared<std::atomic<size_t>>(0)};
  UCompletedJobQueue<AsyncChunkLoadResult> CompletedLoads;
  std::atomic<uint64_t> LoadResultPushWaitNs{0};
  std::atomic<uint64_t> LoadResultPushHeldNs{0};
  std::atomic<uint64_t> LoadResultPushWaitMaxNs{0};
  std::atomic<uint64_t> LoadResultPushHeldMaxNs{0};
  std::atomic<uint64_t> LoadResultPushCount{0};
  UCompletedJobQueue<AsyncChunkSaveRequest> CompletedSaves;
  UCompletedJobQueue<AsyncColumnLightFlagsSaveResult>
      CompletedColumnLightFlagsSaves;
  std::atomic<bool> CancelledLoadSweepPending{false};
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
