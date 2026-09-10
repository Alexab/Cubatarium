#include "World/Streaming/ColumnFlowScheduler.h"
#include "World/Streaming/ColumnEmergeBump.h"

#include <algorithm>

namespace cutum
{

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

void UColumnFlowScheduler::PushLive(const ColumnWorkItem &item)
{
  ColumnWorkItem stamped = item;
  stamped.generation = next_generation_++;
  LiveTicket ticket{};
  ticket.kind = stamped.kind;
  ticket.priority = stamped.priority;
  ticket.scan_full_focus = stamped.scan_full_focus;
  ticket.cy = stamped.cy;
  ticket.generation = stamped.generation;
  live_[ColumnCoord(stamped.column)] = ticket;
  HeapEntry entry{};
  entry.item = stamped;
  entry.sequence = next_sequence_++;
  heap_.push(entry);
}

void UColumnFlowScheduler::Enqueue(const ColumnWorkItem &item)
{
  const ColumnCoord coord(item.column);
  const auto it = live_.find(coord);
  if (it == live_.end())
  {
    PushLive(item);
    return;
  }

  const LiveTicket &old = it->second;
  if (old.kind == item.kind)
  {
    // Same kind: refresh urgency / preferred slice with a new generation.
    // Keep the higher priority; prefer a concrete cy (>=0) over whole-band.
    ColumnWorkItem refreshed = item;
    refreshed.priority = std::max(old.priority, item.priority);
    if (old.cy >= 0 && item.cy < 0)
    {
      refreshed.cy = old.cy;
    }
    // Prefer full-focus scan if either request asked for it.
    refreshed.scan_full_focus = old.scan_full_focus || item.scan_full_focus;
    ++superseded_n_;
    PushLive(refreshed);
    return;
  }

  if (ColumnWorkKindExclusiveRank(item.kind) >
      ColumnWorkKindExclusiveRank(old.kind))
  {
    ++upgrade_n_;
    ++superseded_n_;
    PushLive(item);
    return;
  }

  ++denied_n_;
}

bool UColumnFlowScheduler::DrainOne(ColumnWorkItem &out)
{
  while (!heap_.empty())
  {
    HeapEntry entry = heap_.top();
    heap_.pop();
    const ColumnCoord coord(entry.item.column);
    const auto it = live_.find(coord);
    if (it == live_.end() || it->second.generation != entry.item.generation)
    {
      // Stale / superseded heap residue.
      continue;
    }
    out = entry.item;
    live_.erase(it);
    return true;
  }
  return false;
}

void UColumnFlowScheduler::Clear()
{
  while (!heap_.empty())
  {
    heap_.pop();
  }
  live_.clear();
}

bool UColumnFlowScheduler::Contains(glm::ivec2 column,
                                    ColumnWorkKind kind) const
{
  return Contains(column, kind, false) || Contains(column, kind, true);
}

bool UColumnFlowScheduler::Contains(glm::ivec2 column, ColumnWorkKind kind,
                                    bool scan_full_focus) const
{
  const auto it = live_.find(ColumnCoord(column));
  if (it == live_.end())
  {
    return false;
  }
  return it->second.kind == kind &&
         it->second.scan_full_focus == scan_full_focus;
}

bool UColumnFlowScheduler::ContainsColumn(glm::ivec2 column) const
{
  return live_.count(ColumnCoord(column)) != 0;
}

} // namespace cutum
