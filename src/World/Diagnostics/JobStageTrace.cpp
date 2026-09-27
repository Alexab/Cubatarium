#include "World/Diagnostics/JobStageTrace.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <unordered_set>

namespace cutum
{
namespace
{

struct Ring
{
  std::array<JobStageSpan, UJobStageTrace::kRingCapacity> slots{};
  size_t write{0};
  size_t count{0};
  std::mutex mu;
};

Ring &GetRing()
{
  static Ring r;
  return r;
}

struct VisualChunkKey
{
  int32_t cx{0};
  int32_t cy{0};
  int32_t cz{0};

  bool operator==(const VisualChunkKey &other) const
  {
    return cx == other.cx && cy == other.cy && cz == other.cz;
  }
};

struct VisualChunkKeyHash
{
  size_t operator()(const VisualChunkKey &key) const
  {
    size_t h = static_cast<uint32_t>(key.cx);
    h ^= static_cast<size_t>(static_cast<uint32_t>(key.cy)) +
         0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= static_cast<size_t>(static_cast<uint32_t>(key.cz)) +
         0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
  }
};

struct VisualChunkWatches
{
  static constexpr size_t kCapacity = 1024;
  std::unordered_set<VisualChunkKey, VisualChunkKeyHash> chunks;
  std::deque<VisualChunkKey> order;
  std::mutex mu;
};

VisualChunkWatches &GetVisualChunkWatches()
{
  static VisualChunkWatches watches;
  return watches;
}

bool IsVisualChunkWatched(int32_t cx, int32_t cy, int32_t cz)
{
  auto &watches = GetVisualChunkWatches();
  std::lock_guard<std::mutex> lock(watches.mu);
  return watches.chunks.count({cx, cy, cz}) != 0;
}

struct WatchedJobRing
{
  std::array<JobStageSpan, UJobStageTrace::kVisualLifecycleRingCapacity> slots{};
  size_t write{0};
  size_t count{0};
  std::mutex mu;
};

WatchedJobRing &GetWatchedJobRing()
{
  static WatchedJobRing r;
  return r;
}

struct CullDecisionRing
{
  std::array<JobStageSpan, UJobStageTrace::kCullDecisionRingCapacity> slots{};
  size_t write{0};
  size_t count{0};
  std::mutex mu;
};

CullDecisionRing &GetCullDecisionRing()
{
  static CullDecisionRing r;
  return r;
}

template <size_t Capacity> struct VisualBlackTraceRing
{
  std::array<VisualBlackTraceRecord, Capacity> slots{};
  size_t write{0};
  size_t count{0};
  std::mutex mu;
};

VisualBlackTraceRing<UJobStageTrace::kVisualBlackTraceRingCapacity> &
GetVisualBlackTraceRing()
{
  static VisualBlackTraceRing<UJobStageTrace::kVisualBlackTraceRingCapacity> r;
  return r;
}

VisualBlackTraceRing<UJobStageTrace::kRendererGateTraceRingCapacity> &
GetRendererGateTraceRing()
{
  static VisualBlackTraceRing<UJobStageTrace::kRendererGateTraceRingCapacity> r;
  return r;
}

VisualBlackTraceRing<UJobStageTrace::kFrustumCoverageTraceRingCapacity> &
GetFrustumCoverageTraceRing()
{
  static VisualBlackTraceRing<
      UJobStageTrace::kFrustumCoverageTraceRingCapacity> r;
  return r;
}

VisualBlackTraceRing<UJobStageTrace::kVisualBlackAttributionTraceRingCapacity> &
GetVisualBlackAttributionTraceRing()
{
  static VisualBlackTraceRing<
      UJobStageTrace::kVisualBlackAttributionTraceRingCapacity> r;
  return r;
}

VisualBlackTraceRing<UJobStageTrace::kVisualRepairTraceRingCapacity> &
GetVisualRepairTraceRing()
{
  static VisualBlackTraceRing<UJobStageTrace::kVisualRepairTraceRingCapacity> r;
  return r;
}

VisualBlackTraceRing<UJobStageTrace::kMeshScheduleTraceRingCapacity> &
GetMeshScheduleTraceRing()
{
  static VisualBlackTraceRing<UJobStageTrace::kMeshScheduleTraceRingCapacity> r;
  return r;
}

VisualBlackTraceRing<UJobStageTrace::kPriorityRemeshTraceRingCapacity> &
GetPriorityRemeshTraceRing()
{
  static VisualBlackTraceRing<
      UJobStageTrace::kPriorityRemeshTraceRingCapacity> r;
  return r;
}

template <size_t Capacity>
void PushVisualTrace(VisualBlackTraceRing<Capacity> &ring,
                     const VisualBlackTraceRecord &record)
{
  std::lock_guard<std::mutex> lock(ring.mu);
  ring.slots[ring.write % Capacity] = record;
  ++ring.write;
  if (ring.count < Capacity)
  {
    ++ring.count;
  }
}

template <size_t Capacity>
void ForEachVisualTraceNewest(
    VisualBlackTraceRing<Capacity> &ring, size_t max_n,
    void (*fn)(const VisualBlackTraceRecord &, void *), void *ctx)
{
  std::lock_guard<std::mutex> lock(ring.mu);
  const size_t n = (max_n < ring.count) ? max_n : ring.count;
  for (size_t i = 0; i < n; ++i)
  {
    const size_t abs = (ring.write + Capacity - 1 - i) % Capacity;
    fn(ring.slots[abs], ctx);
  }
}

} // namespace

void UJobStageTrace::Note(const JobStageSpan &span)
{
  auto &r = GetRing();
  {
    std::lock_guard<std::mutex> lock(r.mu);
    r.slots[r.write % kRingCapacity] = span;
    ++r.write;
    if (r.count < kRingCapacity)
    {
      ++r.count;
    }
  }
  if (!VisualBlackTraceEnabled() ||
      !IsVisualChunkWatched(span.cx, span.cy, span.cz))
  {
    return;
  }

  auto &watched = GetWatchedJobRing();
  std::lock_guard<std::mutex> lock(watched.mu);
  watched.slots[watched.write % kVisualLifecycleRingCapacity] = span;
  ++watched.write;
  if (watched.count < kVisualLifecycleRingCapacity)
  {
    ++watched.count;
  }
}

void UJobStageTrace::NoteTerminal(JobStageSpan span,
                                  JobStage terminal_stage,
                                  JobTerminalReason reason)
{
  span.stage = terminal_stage;
  span.terminal_reason = reason;
  span.stage_ms = 0.0;
  if (span.created_ms > 0.0)
  {
    const double now_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now()
                                  .time_since_epoch())
                              .count();
    span.elapsed_ms = now_ms - span.created_ms;
  }
  Note(span);
}

void UJobStageTrace::WatchVisualChunk(int32_t cx, int32_t cy, int32_t cz)
{
  if (!VisualBlackTraceEnabled())
  {
    return;
  }
  auto &watches = GetVisualChunkWatches();
  std::lock_guard<std::mutex> lock(watches.mu);
  const VisualChunkKey key{cx, cy, cz};
  if (!watches.chunks.insert(key).second)
  {
    return;
  }
  watches.order.push_back(key);
  if (watches.order.size() > VisualChunkWatches::kCapacity)
  {
    watches.chunks.erase(watches.order.front());
    watches.order.pop_front();
  }
}

void UJobStageTrace::NoteCullDecision(int32_t cx, int32_t cy, int32_t cz,
                                      uint8_t cull_decision,
                                      uint64_t attempt_id,
                                      uint64_t published_rev)
{
  JobStageSpan span{};
  span.cx = cx;
  span.cy = cy;
  span.cz = cz;
  span.cull_decision = cull_decision;
  span.attempt_id = attempt_id;
  span.published_rev = published_rev;
  span.stage = JobStage::Published;
  span.outcome = cull_decision != 0 ? 1 : 0;
  auto &r = GetCullDecisionRing();
  std::lock_guard<std::mutex> lock(r.mu);
  r.slots[r.write % kCullDecisionRingCapacity] = span;
  ++r.write;
  if (r.count < kCullDecisionRingCapacity)
  {
    ++r.count;
  }
}

size_t UJobStageTrace::Size()
{
  auto &r = GetRing();
  std::lock_guard<std::mutex> lock(r.mu);
  return r.count;
}

bool UJobStageTrace::Get(size_t newest_index, JobStageSpan &out)
{
  auto &r = GetRing();
  std::lock_guard<std::mutex> lock(r.mu);
  if (newest_index >= r.count)
  {
    return false;
  }
  // newest_index 0 = most recent
  const size_t abs =
      (r.write + kRingCapacity - 1 - newest_index) % kRingCapacity;
  out = r.slots[abs];
  return true;
}

void UJobStageTrace::ForEachNewest(size_t max_n,
                                   void (*fn)(const JobStageSpan &, void *),
                                   void *ctx)
{
  if (!fn)
  {
    return;
  }
  auto &r = GetRing();
  std::lock_guard<std::mutex> lock(r.mu);
  const size_t n = (max_n < r.count) ? max_n : r.count;
  for (size_t i = 0; i < n; ++i)
  {
    const size_t abs = (r.write + kRingCapacity - 1 - i) % kRingCapacity;
    fn(r.slots[abs], ctx);
  }
}

void UJobStageTrace::ForEachCullDecisionNewest(
    size_t max_n, void (*fn)(const JobStageSpan &, void *), void *ctx)
{
  if (!fn)
  {
    return;
  }
  auto &r = GetCullDecisionRing();
  std::lock_guard<std::mutex> lock(r.mu);
  const size_t n = (max_n < r.count) ? max_n : r.count;
  for (size_t i = 0; i < n; ++i)
  {
    const size_t abs = (r.write + kCullDecisionRingCapacity - 1 - i) %
                       kCullDecisionRingCapacity;
    fn(r.slots[abs], ctx);
  }
}

bool UJobStageTrace::VisualBlackTraceEnabled()
{
  static const bool enabled = []() {
    const char *env = std::getenv("CUBA_VISUAL_BLACK_TRACE");
    return env != nullptr && env[0] != '\0' && env[0] != '0';
  }();
  return enabled;
}

void UJobStageTrace::NoteVisualBlack(const VisualBlackTraceRecord &record)
{
  // Renderer/focus samples are emitted at frame rate. Keep renderer candidates,
  // repair admission, repair scans, general mesh scheduling, and priority
  // remesh scheduling in separate rings so one workload cannot overwrite
  // another class of evidence.
  if (record.sample_kind == 0)
  {
    // Per-column census attribution is the evidence behind the aggregate VB
    // counter. Give it its own ring so renderer/focus samples cannot evict it.
    PushVisualTrace(GetVisualBlackAttributionTraceRing(), record);
  }
  else if (record.sample_kind == 2)
  {
    PushVisualTrace(GetRendererGateTraceRing(), record);
  }
  else if (record.sample_kind == 8)
  {
    PushVisualTrace(GetFrustumCoverageTraceRing(), record);
  }
  else if (record.sample_kind == 3 || record.sample_kind == 5)
  {
    PushVisualTrace(GetVisualRepairTraceRing(), record);
  }
  else if (record.sample_kind == 7)
  {
    PushVisualTrace(GetPriorityRemeshTraceRing(), record);
  }
  else if (record.sample_kind == 4 || record.sample_kind == 6)
  {
    PushVisualTrace(GetMeshScheduleTraceRing(), record);
  }
  else
  {
    PushVisualTrace(GetVisualBlackTraceRing(), record);
  }
}

void UJobStageTrace::ForEachVisualBlackNewest(
    size_t max_n, void (*fn)(const VisualBlackTraceRecord &, void *), void *ctx)
{
  if (!fn)
  {
    return;
  }
  // Class groups are emitted separately; use frame_epoch to join their
  // records because their bounded rings have independent write sequences.
  ForEachVisualTraceNewest(GetVisualBlackAttributionTraceRing(), max_n, fn,
                           ctx);
  ForEachVisualTraceNewest(GetRendererGateTraceRing(), max_n, fn, ctx);
  ForEachVisualTraceNewest(GetFrustumCoverageTraceRing(), max_n, fn, ctx);
  ForEachVisualTraceNewest(GetVisualRepairTraceRing(), max_n, fn, ctx);
  ForEachVisualTraceNewest(GetMeshScheduleTraceRing(), max_n, fn, ctx);
  ForEachVisualTraceNewest(GetPriorityRemeshTraceRing(), max_n, fn, ctx);
  ForEachVisualTraceNewest(GetVisualBlackTraceRing(), max_n, fn, ctx);
}

void UJobStageTrace::ForEachWatchedNewest(
    size_t max_n, void (*fn)(const JobStageSpan &, void *), void *ctx)
{
  if (!fn)
  {
    return;
  }
  auto &r = GetWatchedJobRing();
  std::lock_guard<std::mutex> lock(r.mu);
  const size_t n = (max_n < r.count) ? max_n : r.count;
  for (size_t i = 0; i < n; ++i)
  {
    const size_t abs =
        (r.write + kVisualLifecycleRingCapacity - 1 - i) %
        kVisualLifecycleRingCapacity;
    fn(r.slots[abs], ctx);
  }
}

const char *UJobStageTrace::StageName(JobStage s)
{
  switch (s)
  {
  case JobStage::Created:
    return "created";
  case JobStage::Admitted:
    return "admitted";
  case JobStage::Started:
    return "started";
  case JobStage::Built:
    return "built";
  case JobStage::Uploaded:
    return "uploaded";
  case JobStage::Published:
    return "published";
  case JobStage::Retired:
    return "retired";
  case JobStage::Cancelled:
    return "cancelled";
  case JobStage::GpuQueued:
    return "gpu_queued";
  case JobStage::GpuKicked:
    return "gpu_kicked";
  case JobStage::GpuCountersReady:
    return "gpu_counters_ready";
  case JobStage::GpuReady:
    return "gpu_ready";
  default:
    return "unknown";
  }
}

const char *UJobStageTrace::TerminalReasonName(JobTerminalReason reason)
{
  switch (reason)
  {
  case JobTerminalReason::None:
    return "none";
  case JobTerminalReason::StaleInputStamp:
    return "stale_input_stamp";
  case JobTerminalReason::StaleCatalog:
    return "stale_catalog";
  case JobTerminalReason::StaleGeometryInput:
    return "stale_geometry_input";
  case JobTerminalReason::StaleLightInput:
    return "stale_light_input";
  case JobTerminalReason::ChunkNotResident:
    return "chunk_not_resident";
  case JobTerminalReason::NoActiveOwner:
    return "no_active_owner";
  case JobTerminalReason::SupersededByNewerRevision:
    return "superseded_by_newer_revision";
  case JobTerminalReason::CurrentRevisionAdvanced:
    return "current_revision_advanced";
  case JobTerminalReason::DarkMeshRejected:
    return "dark_mesh_rejected";
  case JobTerminalReason::GpuAdmissionDeferred:
    return "gpu_admission_deferred";
  case JobTerminalReason::PublicationRejected:
    return "publication_rejected";
  case JobTerminalReason::SoftDeferPriorGpuRetained:
    return "softdefer_prior_gpu_retained";
  case JobTerminalReason::SoftDeferPriorMeshRetained:
    return "softdefer_prior_mesh_retained";
  case JobTerminalReason::SoftDeferRetryRequired:
    return "softdefer_retry_required";
  case JobTerminalReason::SoftDeferFirstMeshHeld:
    return "softdefer_first_mesh_held";
  case JobTerminalReason::ResultMemoryBudgetRejected:
    return "result_memory_budget_rejected";
  case JobTerminalReason::CompletedQueueOverflow:
    return "completed_queue_overflow";
  case JobTerminalReason::CompletedQueueCapacityReduced:
    return "completed_queue_capacity_reduced";
  case JobTerminalReason::SubmissionEpochChanged:
    return "submission_epoch_changed";
  case JobTerminalReason::JobIdentityReplaced:
    return "job_identity_replaced";
  case JobTerminalReason::WorkerPoolRejected:
    return "worker_pool_rejected";
  }
  return "unknown";
}

} // namespace cutum
