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

int64_t ColumnOnlyKey(glm::ivec2 column)
{
  return (static_cast<int64_t>(column.x) << 32) |
         static_cast<uint32_t>(column.y);
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
  const int64_t col_key = ColumnOnlyKey(item.column);
  if (occupied_columns_.count(col_key) != 0)
  {
    const int64_t key =
        ColumnKey(item.column, item.kind, item.scan_full_focus);
    if (inflight_.count(key) != 0)
    {
      return;
    }
    ++denied_n_;
    return;
  }
  const int64_t key =
      ColumnKey(item.column, item.kind, item.scan_full_focus);
  if (inflight_.count(key) != 0)
  {
    return;
  }
  inflight_.insert(key);
  occupied_columns_.insert(col_key);
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
  occupied_columns_.erase(ColumnOnlyKey(out.column));
  return true;
}

void UColumnFlowScheduler::Clear()
{
  while (!queue_.empty())
  {
    queue_.pop();
  }
  inflight_.clear();
  occupied_columns_.clear();
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

bool UColumnFlowScheduler::ContainsColumn(glm::ivec2 column) const
{
  return occupied_columns_.count(ColumnOnlyKey(column)) != 0;
}

} // namespace cutum
