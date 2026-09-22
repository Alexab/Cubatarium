#include "World/Diagnostics/JobStageTrace.h"

#include <array>
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
  default:
    return "unknown";
  }
}

} // namespace cutum
