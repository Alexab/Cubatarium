#pragma once

#include "World/Streaming/ColumnFlowScheduler.h"
#include "World/Streaming/VisualStagePolicy.h"

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
/// Era32 I-L1: fully-dark drawable inside LitDrawable ring is not draw_ok.
inline ColumnSoTDecision ClassifyStickyStaleDarkSoT(
    bool has_mesh_or_gpu, bool sticky, bool stale_dark_with_mesh,
    int horiz_from_focus, bool has_real_repair_ticket = false,
    bool fully_dark_drawable = false,
    int lit_ring = kVisualStageLitDrawableHoriz)
{
  ColumnSoTDecision out;
  if (horiz_from_focus <= 1)
  {
    return out;
  }
  const bool dark_unfinished =
      fully_dark_drawable && horiz_from_focus <= lit_ring;
  if (sticky)
  {
    out.kind = ColumnSoTKind::StickyRemesh;
    out.has_repair_ticket = has_real_repair_ticket || sticky;
    out.draw_ok = has_mesh_or_gpu && !dark_unfinished;
    return out;
  }
  if (stale_dark_with_mesh)
  {
    out.kind = ColumnSoTKind::StaleDark;
    out.has_repair_ticket = has_real_repair_ticket;
    // draw-when-meshed hinterland; LitDrawable ring waits Relight-before-draw.
    out.draw_ok = !dark_unfinished;
    return out;
  }
  return out;
}

/// Era28/32: UnlitFirstMesh only outside LitDrawable ring (horiz > ring).
/// Ring missing waits Relight-before-draw; hinterland may Unlit preview.
inline bool AllowUnlitFirstMesh(bool has_mesh, int horiz_from_focus,
                                bool /*is_nearest_missing*/, bool in_focus,
                                int near_r = kVisualStageLitDrawableHoriz)
{
  if (has_mesh || !in_focus)
  {
    return false;
  }
  return horiz_from_focus > near_r;
}

/// Void-edge / VisibleBlack debt: Relight-first (mesh dark + light field 0).
/// No RemeshSeam — remesh alone cannot invent light (manual 190350 / Era32 P1).
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
  }
}

/// VisibleBlack Hide⇒Ticket: RelightThenMesh (+ Promote) — same as void.
inline void EnqueueVisibleBlackRepairTickets(
    UColumnFlowScheduler &scheduler, glm::ivec2 focus,
    const std::vector<glm::ivec2> &cols)
{
  EnqueueVoidDarkRelightTickets(scheduler, focus, cols);
}

/// Enqueue RemeshSeam / RelightThenMesh tickets for sticky + stale-dark columns
/// (mirrors UColumnFlowExecutor::TickDerived repair section).
/// Era32: fully-dark / void-class stale must not use RemeshSeam-as-heal —
/// caller routes those through EnqueueVoidDarkRelightTickets.
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
  }
  for (const glm::ivec2 &stale_col : stale_dark_cols)
  {
    const int d = near_dist(stale_col);
    const int prio_boost = d <= 2 ? 20 : 0;
    scheduler.Enqueue(stale_col, ColumnWorkKind::RemeshSeam, 28 + prio_boost);
  }
}

} // namespace cutum
