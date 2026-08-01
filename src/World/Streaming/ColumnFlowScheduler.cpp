#include "World/Streaming/ColumnFlowScheduler.h"

namespace cutum
{

namespace
{

int64_t ColumnKey(glm::ivec2 column, ColumnWorkKind kind, bool scan_full_focus)
{
  return (static_cast<int64_t>(column.x) << 32) |
         (static_cast<int64_t>(column.y & 0xffff) << 16) |
         (static_cast<int64_t>(kind) << 1) |
         (scan_full_focus ? 1 : 0);
}

} // namespace

void UColumnFlowScheduler::Enqueue(glm::ivec2 column, ColumnWorkKind kind,
                                   int priority)
{
  ColumnWorkItem item{};
  item.column = column;
  item.kind = kind;
  item.priority = priority;
  item.scan_full_focus = false;
  item.cy = -1;
  Enqueue(item);
}

void UColumnFlowScheduler::Enqueue(const ColumnWorkItem &item)
{
  const int64_t key =
      ColumnKey(item.column, item.kind, item.scan_full_focus);
  if (inflight_.count(key) != 0)
  {
    return;
  }
  inflight_.insert(key);
  queue_.push(item);
}

bool UColumnFlowScheduler::DrainOne(ColumnWorkItem &out)
{
  if (queue_.empty())
  {
    return false;
  }
  out = queue_.top();
  queue_.pop();
  inflight_.erase(ColumnKey(out.column, out.kind, out.scan_full_focus));
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

bool UColumnFlowScheduler::Contains(glm::ivec2 column,
                                    ColumnWorkKind kind) const
{
  return Contains(column, kind, false) || Contains(column, kind, true);
}

bool UColumnFlowScheduler::Contains(glm::ivec2 column, ColumnWorkKind kind,
                                    bool scan_full_focus) const
{
  return inflight_.count(ColumnKey(column, kind, scan_full_focus)) != 0;
}

} // namespace cutum
