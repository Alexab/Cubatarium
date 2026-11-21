#include "World/Streaming/ColumnFlowScheduler.h"
#include "World/Streaming/ColumnEmergeBump.h"

#include <algorithm>

namespace cutum
{
namespace
{
ColumnWorkItem MergeTicketScope(const ColumnWorkItem &item, int old_cy,
                               int old_priority, bool old_full_focus)
{
  ColumnWorkItem merged = item;
  // One cy cannot encode two distinct slice/range obligations. The full
  // column is the conservative union, independent of the current world band.
  if (old_cy != item.cy)
    merged.cy = kColumnWorkWholeColumnCy;
  merged.priority = std::max(old_priority, item.priority);
  merged.scan_full_focus = old_full_focus || item.scan_full_focus;
  return merged;
}
} // namespace

bool UColumnFlowScheduler::Enqueue(glm::ivec2 column, ColumnWorkKind kind,
                                   int priority)
{
  ColumnWorkItem item{};
  item.column = column;
  item.kind = kind;
  item.priority = priority;
  item.scan_full_focus = false;
  item.cy = kColumnWorkWholeRemeshBandCy;
  return Enqueue(item);
}

void UColumnFlowScheduler::PushLive(const ColumnWorkItem &item)
{
  RemoveColumn(item.column);
  ColumnWorkItem stamped = item;
  stamped.generation = next_generation_++;
  LiveTicket ticket{};
  ticket.kind = stamped.kind;
  ticket.priority = stamped.priority;
  ticket.scan_full_focus = stamped.scan_full_focus;
  ticket.cy = stamped.cy;
  ticket.generation = stamped.generation;
  ticket.sequence = next_sequence_++;
  Live[ColumnCoord(stamped.column)] = ticket;
  HeapEntry entry{};
  entry.item = stamped;
  entry.sequence = ticket.sequence;
  Heap.insert(entry);
}

bool UColumnFlowScheduler::Enqueue(const ColumnWorkItem &item)
{
  const ColumnCoord coord(item.column);
  const auto it = Live.find(coord);
  if (it == Live.end())
  {
    if (Live.size() >= kMaxLiveTickets)
    {
      ++capacity_refused_n_;
      return false;
    }
    PushLive(item);
    return true;
  }

  const LiveTicket &old = it->second;
  if (old.kind == item.kind)
  {
    const auto refreshed =
        MergeTicketScope(item, old.cy, old.priority, old.scan_full_focus);
    if (refreshed.priority == old.priority && refreshed.cy == old.cy &&
        refreshed.scan_full_focus == old.scan_full_focus)
    {
      return true; // Existing live ticket already owns this identical scope.
    }
    ++superseded_n_;
    PushLive(refreshed);
    return true;
  }

  if (ColumnWorkKindExclusiveRank(item.kind) >
      ColumnWorkKindExclusiveRank(old.kind))
  {
    ++upgrade_n_;
    ++superseded_n_;
    PushLive(MergeTicketScope(item, old.cy, old.priority, old.scan_full_focus));
    return true;
  }

  ++denied_n_;
  return false;
}

bool UColumnFlowScheduler::ReplaceColumnTicket(const ColumnWorkItem &item,
                                               ColumnWorkKind expected_kind)
{
  const ColumnCoord coord(item.column);
  const auto it = Live.find(coord);
  if (it == Live.end() || it->second.kind != expected_kind)
  {
    return false;
  }
  ++superseded_n_;
  ++upgrade_n_;
  const auto &old = it->second;
  PushLive(MergeTicketScope(item, old.cy, old.priority, old.scan_full_focus));
  return true;
}

bool UColumnFlowScheduler::DrainOne(ColumnWorkItem &out)
{
  if (Heap.empty())
    return false;
  const auto first = Heap.begin();
  out = first->item;
  Heap.erase(first);
  Live.erase(ColumnCoord(out.column));
  return true;
}

void UColumnFlowScheduler::Clear()
{
  Heap.clear();
  Live.clear();
}

bool UColumnFlowScheduler::RemoveColumn(glm::ivec2 column)
{
  const auto it = Live.find(ColumnCoord(column));
  if (it == Live.end())
    return false;
  HeapEntry entry{};
  entry.item.priority = it->second.priority;
  entry.sequence = it->second.sequence;
  Heap.erase(entry);
  Live.erase(it);
  return true;
}

bool UColumnFlowScheduler::Contains(glm::ivec2 column,
                                    ColumnWorkKind kind) const
{
  return Contains(column, kind, false) || Contains(column, kind, true);
}

bool UColumnFlowScheduler::Contains(glm::ivec2 column, ColumnWorkKind kind,
                                    bool scan_full_focus) const
{
  const auto it = Live.find(ColumnCoord(column));
  if (it == Live.end())
  {
    return false;
  }
  return it->second.kind == kind &&
         it->second.scan_full_focus == scan_full_focus;
}

bool UColumnFlowScheduler::ContainsColumn(glm::ivec2 column) const
{
  return Live.count(ColumnCoord(column)) != 0;
}

bool UColumnFlowScheduler::GetLiveTicket(glm::ivec2 column,
                                         ColumnWorkItem &out) const
{
  const auto it = Live.find(ColumnCoord(column));
  if (it == Live.end())
  {
    return false;
  }

  out.column = column;
  out.kind = it->second.kind;
  out.priority = it->second.priority;
  out.scan_full_focus = it->second.scan_full_focus;
  out.cy = it->second.cy;
  out.generation = it->second.generation;
  return true;
}

} // namespace cutum
