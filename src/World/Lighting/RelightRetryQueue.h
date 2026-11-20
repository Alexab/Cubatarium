#pragma once

#include "World/Lighting/ChunkRelightSnapshot.h"
#include <deque>

namespace cutum
{
/// Main-thread owner of exact obligations while workers are stopped.
class RelightRetryQueue
{
public:
  void Append(std::vector<RelightJobSpec> jobs)
  {
    for (auto &job : jobs) Jobs.push_back(std::move(job));
  }
  void Suspend() { Suspended = true; }
  void Resume() { Suspended = false; }
  void Clear() { Jobs.clear(); }
  bool Empty() const { return Jobs.empty(); }
  bool IsSuspended() const { return Suspended; }
  size_t Size() const { return Jobs.size(); }
  template <typename Submit> void Drain(size_t limit, Submit submit)
  {
    if (Suspended) return;
    while (limit-- && !Jobs.empty())
    {
      // The callback may throw or reject admission; retain the original then.
      if (!submit(Jobs.front())) break;
      Jobs.pop_front();
    }
  }
private:
  bool Suspended{false};
  std::deque<RelightJobSpec> Jobs;
};
}
