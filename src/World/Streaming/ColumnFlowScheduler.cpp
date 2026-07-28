#include "World/Streaming/ColumnFlowScheduler.h"

namespace cutum
{

namespace
{

int64_t ColumnKey(glm::ivec2 column, ColumnWorkKind kind)
{
  return (static_cast<int64_t>(column.x) << 32) |
         (static_cast<int64_t>(column.y & 0xffff) << 16) |
         static_cast<int64_t>(kind);
}

} // namespace

void UColumnFlowScheduler::Enqueue(glm::ivec2 column, ColumnWorkKind kind,
                                   int priority)
{
  const int64_t key = ColumnKey(column, kind);
  if (inflight_.count(key) != 0)
  {
    return;
  }
  inflight_.insert(key);
  queue_.push(ColumnWorkItem{column, kind, priority});
}

bool UColumnFlowScheduler::DrainOne(ColumnWorkItem &out)
{
  if (queue_.empty())
  {
    return false;
  }
  out = queue_.top();
  queue_.pop();
  inflight_.erase(ColumnKey(out.column, out.kind));
  return true;
}

void UColumnFlowScheduler::Clear()
{
  while (!queue_.empty())
  {
    queue_.pop();
  }
  inflight_.clear();
}

} // namespace cutum
