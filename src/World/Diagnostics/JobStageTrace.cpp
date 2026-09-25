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

struct VisualBlackTraceRing
{
  std::array<VisualBlackTraceRecord,
             UJobStageTrace::kVisualBlackTraceRingCapacity>
      slots{};
  size_t write{0};
  size_t count{0};
  std::mutex mu;
};

VisualBlackTraceRing &GetVisualBlackTraceRing()
{
  static VisualBlackTraceRing r;
  return r;
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
  auto &r = GetVisualBlackTraceRing();
  std::lock_guard<std::mutex> lock(r.mu);
  r.slots[r.write % kVisualBlackTraceRingCapacity] = record;
  ++r.write;
  if (r.count < kVisualBlackTraceRingCapacity)
  {
    ++r.count;
  }
}

void UJobStageTrace::ForEachVisualBlackNewest(
    size_t max_n, void (*fn)(const VisualBlackTraceRecord &, void *), void *ctx)
{
  if (!fn)
  {
    return;
  }
  auto &r = GetVisualBlackTraceRing();
  std::lock_guard<std::mutex> lock(r.mu);
  const size_t n = (max_n < r.count) ? max_n : r.count;
  for (size_t i = 0; i < n; ++i)
  {
    const size_t abs =
        (r.write + kVisualBlackTraceRingCapacity - 1 - i) %
        kVisualBlackTraceRingCapacity;
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

} // namespace cutum
