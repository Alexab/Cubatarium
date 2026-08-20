#include "World/Streaming/ColumnFlowScheduler.h"
#include "World/Streaming/ColumnEmergeBump.h"

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
  const int64_t key =
      ColumnKey(item.column, item.kind, item.scan_full_focus);
  if (occupied_columns_.count(col_key) != 0)
  {
    if (inflight_.count(key) != 0)
    {
      return; // same kind already queued
    }
    const auto kit = occupied_kind_.find(col_key);
    const ColumnWorkKind old_kind =
        kit != occupied_kind_.end() ? kit->second : ColumnWorkKind::RemeshSeam;
    if (ColumnWorkKindExclusiveRank(item.kind) >
        ColumnWorkKindExclusiveRank(old_kind))
    {
      // Cancel both scan_full_focus variants of the old kind.
      cancelled_keys_.insert(ColumnKey(item.column, old_kind, false));
      cancelled_keys_.insert(ColumnKey(item.column, old_kind, true));
      inflight_.erase(ColumnKey(item.column, old_kind, false));
      inflight_.erase(ColumnKey(item.column, old_kind, true));
      occupied_kind_[col_key] = item.kind;
      inflight_.insert(key);
      queue_.push(item);
      ++upgrade_n_;
      return;
    }
    ++denied_n_;
    return;
  }
  if (inflight_.count(key) != 0)
  {
    return;
  }
  inflight_.insert(key);
  occupied_columns_.insert(col_key);
  occupied_kind_[col_key] = item.kind;
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
    if (cancelled_keys_.erase(key) > 0)
    {
      continue; // superseded by rank upgrade
    }
    inflight_.erase(key);
    const int64_t col_key = ColumnOnlyKey(out.column);
    occupied_columns_.erase(col_key);
    occupied_kind_.erase(col_key);
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
  inflight_.clear();
  occupied_columns_.clear();
  occupied_kind_.clear();
  cancelled_keys_.clear();
}

bool UColumnFlowScheduler::Contains(glm::ivec2 column,
                                    ColumnWorkKind kind) const
{
  return Contains(column, kind, false) || Contains(column, kind, true);
}

bool UColumnFlowScheduler::Contains(glm::ivec2 column, ColumnWorkKind kind,
                                    bool scan_full_focus) const
{
  const int64_t key = ColumnKey(column, kind, scan_full_focus);
  if (cancelled_keys_.count(key) > 0)
  {
    return false;
  }
  return inflight_.count(key) != 0;
}

bool UColumnFlowScheduler::ContainsColumn(glm::ivec2 column) const
{
  return occupied_columns_.count(ColumnOnlyKey(column)) != 0;
}

} // namespace cutum
