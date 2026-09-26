#include "World/Diagnostics/JobStageTrace.h"

#include <array>
#include <cstdlib>
#include <mutex>

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
  std::lock_guard<std::mutex> lock(r.mu);
  r.slots[r.write % kRingCapacity] = span;
  ++r.write;
  if (r.count < kRingCapacity)
  {
    ++r.count;
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
  // Renderer/focus samples are emitted at frame rate. Keep repair admission,
  // repair scans, general mesh scheduling, and priority remesh scheduling in
  // separate rings so busy first-mesh work cannot overwrite repair outcomes.
  if (record.sample_kind == 3 || record.sample_kind == 5)
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
  ForEachVisualTraceNewest(GetVisualRepairTraceRing(), max_n, fn, ctx);
  ForEachVisualTraceNewest(GetMeshScheduleTraceRing(), max_n, fn, ctx);
  ForEachVisualTraceNewest(GetPriorityRemeshTraceRing(), max_n, fn, ctx);
  ForEachVisualTraceNewest(GetVisualBlackTraceRing(), max_n, fn, ctx);
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

} // namespace cutum
