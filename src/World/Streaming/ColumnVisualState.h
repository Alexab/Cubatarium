#pragma once

#include <cstdint>

namespace cutum
{

/// Column visual ownership for mesh+light co-publish (sysreset after 27beca1c).
/// Replaces PreferKick-without-Dirty sole-owner spin: FullyDark always lands in
/// NeedRelight|NeedRemesh with a scheduled producer, then Publishing→Ready.
enum class ColumnVisualState : uint8_t
{
  Ready = 0,
  NeedRelight = 1,
  NeedRemesh = 2,
  Publishing = 3,
  PublishedEmpty = 4,
};

inline int ColumnVisualStateRank(ColumnVisualState s)
{
  return static_cast<int>(s);
}

/// FullyDark with drawable ⇒ need both light and remesh schedule.
inline ColumnVisualState ColumnVisualStateForFullyDarkDrawable(bool fully_dark,
                                                              bool has_drawable)
{
  if (fully_dark && has_drawable)
  {
    return ColumnVisualState::NeedRelight;
  }
  if (fully_dark)
  {
    return ColumnVisualState::NeedRemesh;
  }
  return ColumnVisualState::Ready;
}

/// PreferKick is legal only while Publishing and Apply made progress.
inline bool ColumnVisualAllowsPreferKick(ColumnVisualState state,
                                        bool has_publish_progress)
{
  return state == ColumnVisualState::Publishing && has_publish_progress;
}

/// Ticket without PL/fifo is illegal while FullyDark owes repair.
inline bool ColumnVisualForbidsTicketWithoutFifo(ColumnVisualState state,
                                                 bool has_repair_ticket,
                                                 bool has_pl_or_fifo)
{
  if (!has_repair_ticket)
  {
    return false;
  }
  if (state != ColumnVisualState::NeedRelight &&
      state != ColumnVisualState::NeedRemesh)
  {
    return false;
  }
  return !has_pl_or_fifo;
}

} // namespace cutum
