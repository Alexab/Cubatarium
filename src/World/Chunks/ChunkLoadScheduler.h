#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include "Core/Memory/AllocationCountingResource.h"
#include "Core/Memory/SharedCountingAllocator.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "WorldGen/Core/IUChunkPopulator.h"
#include <chrono>
#include <atomic>
#include <memory>
#include <cstdint>
#include <functional>
#include <set>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UBlockWorld;
class UChunkMeshCache;
class UCore;

enum class ChunkLoadState
{
  Absent,
  Requested,
  Generating,
  Ready,
  Committed
};

class UChunkLoadScheduler
{
public:
  static constexpr size_t kMaxRetainedGenerationResults = 8;
  static constexpr size_t kMaxActiveGenerationRequests = 1024;
  static constexpr size_t kSettingsPayloadBudget = 16*1024*1024;
  size_t SettingsPayloadBytes() const { return SettingsBudget->used.load(std::memory_order_relaxed); }
  size_t SettingsPayloadPeak() const { return SettingsBudget->peak.load(std::memory_order_relaxed); }
  size_t SettingsPayloadLimit() const { return std::max(kSettingsPayloadBudget,SettingsBudget->largest_single); }
  static constexpr size_t kBufferByteBudget=128*1024*1024;
  size_t BufferReservedBytes() const { return BufferBudget->UsedBytes(); }
  size_t BufferReservedPeak() const { return BufferBudget->PeakBytes(); }
  size_t BufferBudgetLimit() const { return BufferBudget->LimitBytes(); }
  UAllocationCountingResource::Snapshot GetControlAllocationSnapshot() const
  { return ControlMemory->GetSnapshot(); }
  uint64_t CapacityRefusedCount() const { return CapacityRefused; }
  void SetMetadataCapacity(size_t capacity) { MetadataCapacity=capacity; }
  size_t MetadataCount() const { return States.size(); }
  struct MetadataStorageSnapshot
  {
    UAllocationCountingResource::Snapshot containers;
    // Character capacity outside inline strings; allocator/OS overhead excluded.
    size_t pending_string_capacity_bytes{0};
  };
  // Owner-thread diagnostic scan, not part of per-frame maintenance.
  MetadataStorageSnapshot GetMetadataStorageSnapshot() const;
  size_t RetireCommittedOutsideInterest(glm::ivec3 center,int radius,
      const std::function<bool(glm::ivec3)> &has_owner,size_t budget=32);
  size_t GetRetainedGenerationResultCount() const
  { return RetainedResults->load(std::memory_order_relaxed); }
  using MarkChunkDirtyFn =
      std::function<void(glm::ivec3 coord, int minY, int maxY,
                         bool fluidSealed)>;
  using ColumnMeshDirtyFn =
      std::function<void(glm::ivec3 groundCoord, int minY, int maxY)>;

  UChunkLoadScheduler(IUChunkPopulator &populator,
                      UChunkGenerationRegistry &tokens);

  void SetMarkDirtyFn(MarkChunkDirtyFn fn);
  void SetColumnMeshDirtyFn(ColumnMeshDirtyFn fn);
  // False transfers no ownership; caller retains/revisits required-data demand.
  bool RequestLoad(glm::ivec3 coord, int priority,
                   const ProceduralSettings &settings,
                   glm::ivec2 column_origin = glm::ivec2(0),
                   bool has_column_origin = false,
                   uint64_t demand_id = 0);
  int CancelPendingOutsideRadius(glm::ivec3 center, int radius_chunks);
  void Cancel(glm::ivec3 coord, const char *reason = "cancelled");
  void CancelAllPending(std::chrono::milliseconds worker_wait =
                            std::chrono::milliseconds(2000),
                        const char *reason = "cancelled_all");
  bool WaitForWorkersIdle(std::chrono::milliseconds timeout =
                               std::chrono::milliseconds(10000));
  /// Cancel + short wait, then detach leftover workers for process exit.
  void ShutdownForProcessExit(std::chrono::milliseconds timeout =
                                  std::chrono::milliseconds(250));
  void Invalidate(glm::ivec3 coord);
  void Tick(UBlockWorld &world, int maxCommitsPerFrame,
            int maxGenerationStartsPerFrame = 4,
            double maxApplyMsPerFrame = 0.0,
            bool allowBoundedReadyDrain = false);
  /// Admit already-requested generation work without applying completed
  /// results. Used by bounded fallback paths that cannot run a full Tick.
  int StartQueuedGenerationWorkers(int maxStarts);
  int GetAvailableGenerationWorkerSlots() const;
  bool IsCommitted(glm::ivec3 coord) const;
  bool IsPending(glm::ivec3 coord) const;
  ChunkLoadState GetState(glm::ivec3 coord) const;
  int GetPendingQueueCount() const;
  int GetGenInFlightCount() const;
  int GetCompletedReadyCount() const;
  size_t GetCompletedReadyPayloadBytes() const;
  int GetGenBacklogTotal() const;
  double GetLastTickApplyMs() const { return LastTickApplyMs; }
  /// Era25: gen commits applied during last Tick (frame honesty).
  int GetLastCommitsThisFrame() const { return LastCommitsThisFrame; }

private:
  friend void RunTerrainSaveFailureProbe(UCore &core);
  struct SettingsBudgetState
  {
    std::atomic<size_t> used{0},peak{0};
    size_t largest_single{0}; // owner thread; no worker writes this limit
  };
  struct OwnedSettings
  {
    explicit OwnedSettings(const ProceduralSettings &source) : value(source) {}
    ProceduralSettings value;
    std::shared_ptr<SettingsBudgetState> budget;
    size_t charge{0};
    ~OwnedSettings() { if(budget) budget->used.fetch_sub(charge,std::memory_order_relaxed); }
  };
  struct PendingRequest
  {
    glm::ivec3 coord;
    uint64_t requestId{0};
    uint64_t demandId{0};
    int priority{0};
    ChunkGenerationToken token;
    std::chrono::steady_clock::time_point requestedAt{};
    uint64_t queueRevision{0};
    std::shared_ptr<const OwnedSettings> settings;
    glm::ivec2 columnOrigin{0};
    bool hasColumnOrigin{false};
  };

  struct ResultSlotGuard
  {
    std::shared_ptr<std::atomic<size_t>> count;
    ~ResultSlotGuard() { count->fetch_sub(1,std::memory_order_relaxed); }
  };
  struct PendingResult
  {
    std::shared_ptr<ResultSlotGuard> resultSlot;
    ChunkPopulateResult result;
    std::optional<PendingRequest> bufferRetry;
    uint64_t requestId{0};
    uint64_t demandId{0};
    int priority{0};
    int maxHeight{256};
    std::chrono::steady_clock::time_point requestedAt{};
    std::chrono::steady_clock::time_point scheduledAt{};
    std::chrono::steady_clock::time_point generationStartedAt{};
    std::chrono::steady_clock::time_point generationFinishedAt{};
    double generationMs{0.0};
    std::size_t requestQueueLiveAtSchedule{0};
    std::size_t requestQueueHeapAtSchedule{0};
    std::size_t workerPendingAtSubmit{0};
    std::size_t workerActiveAtSubmit{0};
    std::size_t workerCount{0};
    int generationStartCapPerFrame{0};
  };

  struct RequestCompare
  {
    bool operator()(const PendingRequest &a, const PendingRequest &b) const
    {
      if (a.priority != b.priority) return a.priority < b.priority;
      if (a.requestedAt != b.requestedAt) return a.requestedAt < b.requestedAt;
      return a.queueRevision < b.queueRevision;
    }
  };

  bool ScheduleWorker(const PendingRequest &request,
                      int generation_start_cap_per_frame);
  void EraseQueuedRequest(glm::ivec3 coord);

  struct StateRecord
  {
    ChunkLoadState state{ChunkLoadState::Absent};
    uint64_t sequence{0};
  };
  struct CoordinateLess
  {
    bool operator()(glm::ivec3 a,glm::ivec3 b) const
    { return a.x!=b.x ? a.x<b.x : a.z!=b.z ? a.z<b.z : a.y<b.y; }
  };
  std::shared_ptr<SettingsBudgetState> SettingsBudget{std::make_shared<SettingsBudgetState>()};
  std::shared_ptr<UConcurrentAllocationCountingResource> ControlMemory{
      std::make_shared<UConcurrentAllocationCountingResource>()};
  std::shared_ptr<UBufferAllocationBudget> BufferBudget{std::make_shared<UBufferAllocationBudget>(kBufferByteBudget)};
  std::atomic<bool> BufferSerialMode{false};
  IUChunkPopulator &Populator;
  UChunkGenerationRegistry &Tokens;
  // A result keeps its slot while queued/running/ready or held by Tick.
  // Independent of the shared compute slot, which ends with the worker.
  std::shared_ptr<std::atomic<size_t>> RetainedResults{
      std::make_shared<std::atomic<size_t>>(0)};
  // Completed must outlive Pool (members destroy in reverse declaration order).
  UCompletedJobQueue<PendingResult> Completed;
  UJobThreadPool Pool;
  MarkChunkDirtyFn MarkDirty;
  ColumnMeshDirtyFn ColumnMeshDirty;
  UAllocationCountingResource MetadataMemory;
  std::pmr::set<PendingRequest, RequestCompare> Queue{&MetadataMemory};
  // One exact removable ordered index entry per authoritative live request.
  std::pmr::unordered_map<glm::ivec3, PendingRequest, IVec3Hash> QueuedRequests{&MetadataMemory};
  std::pmr::map<glm::ivec3,StateRecord,CoordinateLess> States{&MetadataMemory};
  size_t MetadataCapacity{2*kMaxActiveGenerationRequests};
  std::optional<glm::ivec3> MetadataRetirementCursor;
  std::pmr::unordered_map<glm::ivec3, ChunkGenerationToken, IVec3Hash> ActiveTokens{&MetadataMemory};
  std::pmr::unordered_map<glm::ivec3, uint64_t, IVec3Hash> ActiveRequestIds{&MetadataMemory};
  std::pmr::unordered_map<glm::ivec3, int, IVec3Hash> RequestPriorities{&MetadataMemory};
  std::pmr::unordered_map<glm::ivec3, int, IVec3Hash> InitialRequestPriorities{&MetadataMemory};
  std::pmr::unordered_map<glm::ivec3, uint64_t, IVec3Hash>
      RequestPriorityRefreshCounts{&MetadataMemory};
  uint64_t NextRequestQueueRevision{1};
  uint64_t CapacityRefused{0};
  double LastTickApplyMs{0.0};
  int LastCommitsThisFrame{0};
};

} // namespace cutum
