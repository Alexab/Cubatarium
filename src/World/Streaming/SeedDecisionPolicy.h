#pragma once

namespace cutum
{

/// Pure commit-time skylight seed policy (V3): decide sync seed vs PendingLight
/// FIFO without SoftDefer bypass. Cruise near-focus may try cheap budgeted
/// sync (ApplyGpuSkylightSeed / tight budget — never full RelightTerrainColumn
/// under load). Idle underfeet/near-focus may try budgeted sync; fail →
/// PendingLight (never silent LitReady).
struct SeedDecisionInput
{
  bool underfeet{false};
  bool near_focus{false};
  bool can_seed{false};
  bool moving_cruise{false};
  double frame_ms{0.0};
  int visual_holes{0};
  int pending_light_focus{0};
};

struct SeedDecision
{
  bool try_sync_seed{false};
  bool enqueue_pending{false};
  double budget_ms{0.0};
  bool priority_fifo{false};
  /// When true, backends must use cheap column seed (not RelightTerrainColumn).
  bool cheap_seed{false};
};

inline SeedDecision EvaluateSeedDecision(const SeedDecisionInput &in)
{
  SeedDecision out;
  out.priority_fifo =
      in.underfeet || (in.near_focus && in.pending_light_focus <= 20) ||
      (in.near_focus && in.can_seed);

  // Cruise near-focus: cheap budgeted seed when frame has headroom (plan R1).
  // Hot cruise → priority FIFO only (RelightTerrainColumn is 1–7s under load).
  if (in.moving_cruise)
  {
    if (in.near_focus && in.can_seed && in.frame_ms <= 20.0)
    {
      out.try_sync_seed = true;
      out.cheap_seed = true;
      out.budget_ms = in.underfeet ? 1.5 : 2.0;
      return out;
    }
    out.enqueue_pending = true;
    out.priority_fifo = in.near_focus || in.underfeet;
    return out;
  }

  // Idle underfeet: full sync seed (F2 cold).
  if (in.underfeet && in.can_seed && in.frame_ms <= 16.0 &&
      in.visual_holes == 0)
  {
    out.try_sync_seed = true;
    out.budget_ms = 3.0;
    return out;
  }

  // Idle near-focus with neighborhood: budgeted sync only when frame has
  // headroom. Unbounded near-focus Relight during load hung edge.
  if (in.near_focus && in.can_seed && in.frame_ms <= 20.0)
  {
    out.try_sync_seed = true;
    out.budget_ms = in.underfeet ? 3.0 : 2.0;
    return out;
  }

  if (in.near_focus || in.underfeet)
  {
    out.enqueue_pending = true;
    return out;
  }

  out.enqueue_pending = true;
  return out;
}

} // namespace cutum
