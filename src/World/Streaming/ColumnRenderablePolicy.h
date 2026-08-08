#pragma once

#include "World/Streaming/ColumnFlowScheduler.h"

#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

/// Pure SoT for sticky / stale-dark columns (TD-ARCH-026).
/// Hide only when there is no mesh; ticket always accompanies sticky/stale.
enum class ColumnSoTKind : uint8_t
{
  None = 0,
  StickyRemesh,
  StaleDark,
};

struct ColumnSoTDecision
{
  bool draw_ok{false};
  bool has_repair_ticket{false};
  ColumnSoTKind kind{ColumnSoTKind::None};
};

/// horiz>1 sticky/stale-dark path used by GetColumnRenderableState.
/// Era16 TD-052: has_real_repair_ticket must reflect ColumnFlow Contains and/or
/// StickyRemesh membership — never claim a ticket that does not exist.
inline ColumnSoTDecision ClassifyStickyStaleDarkSoT(
    bool has_mesh_or_gpu, bool sticky, bool stale_dark_with_mesh,
    int horiz_from_focus, bool has_real_repair_ticket = false)
{
  ColumnSoTDecision out;
  if (horiz_from_focus <= 1)
  {
    return out;
  }
  if (sticky)
  {
    out.kind = ColumnSoTKind::StickyRemesh;
    out.has_repair_ticket = has_real_repair_ticket || sticky;
    out.draw_ok = has_mesh_or_gpu;
    return out;
  }
  if (stale_dark_with_mesh)
  {
    out.kind = ColumnSoTKind::StaleDark;
    out.has_repair_ticket = has_real_repair_ticket;
    out.draw_ok = true; // draw-when-meshed; SoftDefer still blocks dark first-mesh
    return out;
  }
  return out;
}

/// Explicit SoT contract: FOV missing may first-mesh while PendingLight
/// (UnlitFirstMesh / dark preview). Remesh while pending stays deferred.
/// Full focus missing (not only nearest): SoftDefer rim waits on Capture
/// otherwise leave miss=1 for many periods (land_fix_P1c timeline).
inline bool AllowUnlitFirstMesh(bool has_mesh, int /*horiz_from_focus*/,
                                bool /*is_nearest_missing*/, bool in_focus)
{
  if (has_mesh || !in_focus)
  {
    return false;
  }
  return true;
}

/// VisibleBlack Hide⇒Ticket: RemeshSeam (+ near Relight) without full
/// RelightThenMesh storm used by sticky/stale waves.
inline void EnqueueVisibleBlackRepairTickets(
    UColumnFlowScheduler &scheduler, glm::ivec2 focus,
    const std::vector<glm::ivec2> &cols)
{
  auto near_dist = [&](glm::ivec2 col) {
    return std::max(std::abs(col.x - focus.x), std::abs(col.y - focus.y));
  };
  for (const glm::ivec2 &col : cols)
  {
    const int d = near_dist(col);
    const int prio_boost = d <= 2 ? 20 : 0;
    scheduler.Enqueue(col, ColumnWorkKind::RemeshSeam, 28 + prio_boost);
    if (d <= 2)
    {
      scheduler.Enqueue(col, ColumnWorkKind::PromoteRelight, 35 + prio_boost);
    }
  }
}

/// Enqueue RemeshSeam / RelightThenMesh tickets for sticky + stale-dark columns
/// (mirrors UColumnFlowExecutor::TickDerived repair section).
inline void EnqueueStickyStaleRepairTickets(
    UColumnFlowScheduler &scheduler, glm::ivec2 focus,
    const std::vector<glm::ivec2> &sticky_cols,
    const std::vector<glm::ivec2> &stale_dark_cols)
{
  auto near_dist = [&](glm::ivec2 col) {
    return std::max(std::abs(col.x - focus.x), std::abs(col.y - focus.y));
  };
  for (const glm::ivec2 &col : sticky_cols)
  {
    scheduler.Enqueue(col, ColumnWorkKind::RemeshSeam, 30);
    if (near_dist(col) <= 2)
    {
      scheduler.Enqueue(col, ColumnWorkKind::RelightThenMesh, 45);
      scheduler.Enqueue(col, ColumnWorkKind::PromoteRelight, 40);
    }
  }
  for (const glm::ivec2 &col : stale_dark_cols)
  {
    const int d = near_dist(col);
    const int prio_boost = d <= 2 ? 20 : 0;
    scheduler.Enqueue(col, ColumnWorkKind::RelightThenMesh, 40 + prio_boost);
    scheduler.Enqueue(col, ColumnWorkKind::PromoteRelight, 35 + prio_boost);
    scheduler.Enqueue(col, ColumnWorkKind::RemeshSeam, 28 + prio_boost);
  }
}

/// Void-edge debt: Relight-first (mesh dark + light field 0). No RemeshSeam —
/// remesh alone cannot invent light (manual 190350 void≈610).
inline void EnqueueVoidDarkRelightTickets(
    UColumnFlowScheduler &scheduler, glm::ivec2 focus,
    const std::vector<glm::ivec2> &void_dark_cols)
{
  auto near_dist = [&](glm::ivec2 col) {
    return std::max(std::abs(col.x - focus.x), std::abs(col.y - focus.y));
  };
  for (const glm::ivec2 &col : void_dark_cols)
  {
    const int d = near_dist(col);
    const int prio_boost = d <= 2 ? 25 : 0;
    scheduler.Enqueue(col, ColumnWorkKind::RelightThenMesh, 50 + prio_boost);
    scheduler.Enqueue(col, ColumnWorkKind::PromoteRelight, 45 + prio_boost);
  }
}

} // namespace cutum
