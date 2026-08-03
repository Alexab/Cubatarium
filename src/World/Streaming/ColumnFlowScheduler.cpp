#include "World/Streaming/ColumnFlowScheduler.h"

namespace cutum
{

int64_t UColumnFlowScheduler::ColumnKey(glm::ivec2 column, ColumnWorkKind kind,
                                        bool scan_full_focus)
{
  return (static_cast<int64_t>(column.x) << 32) |
         (static_cast<int64_t>(column.y & 0xffff) << 16) |
         (static_cast<int64_t>(kind) << 1) |
         (scan_full_focus ? 1 : 0);
}

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
  const auto it = best_prio_.find(key);
  if (it != best_prio_.end())
  {
    // S5: bump priority for pinned/near FirstMesh — old dedupe kept prio=10
    // forever while rim pin asked for 100 (manual 163318 far sticky).
    if (it->second >= item.priority)
    {
      return;
    }
    it->second = item.priority;
    queue_.push(item);
    return;
  }
  best_prio_.emplace(key, item.priority);
  queue_.push(item);
}

bool UColumnFlowScheduler::DrainOne(ColumnWorkItem &out)
{
  while (!queue_.empty())
  {
    out = queue_.top();
    queue_.pop();
    const int64_t key =
        ColumnKey(out.column, out.kind, out.scan_full_focus);
    const auto it = best_prio_.find(key);
    if (it == best_prio_.end() || it->second != out.priority)
    {
      continue; // stale after priority bump
    }
    best_prio_.erase(it);
    return true;
  }
  return false;
}

void UColumnFlowScheduler::Clear()
{
  while (!queue_.empty())
  {
    queue_.pop();
  }
  best_prio_.clear();
}

bool UColumnFlowScheduler::Contains(glm::ivec2 column,
                                    ColumnWorkKind kind) const
{
  return Contains(column, kind, false) || Contains(column, kind, true);
}

bool UColumnFlowScheduler::Contains(glm::ivec2 column, ColumnWorkKind kind,
                                    bool scan_full_focus) const
{
  return best_prio_.count(ColumnKey(column, kind, scan_full_focus)) != 0;
}

} // namespace cutum
