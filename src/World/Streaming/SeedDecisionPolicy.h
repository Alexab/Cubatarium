#pragma once

namespace cutum
{

/// Pure commit-time skylight seed policy (V3): decide sync seed vs PendingLight
/// FIFO without SoftDefer bypass. Cruise never sync-Relights (1–7s hitches);
/// near-focus cruise uses priority FIFO. Idle underfeet/near-focus may try
/// budgeted sync; fail → PendingLight (never silent LitReady).
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
};

inline SeedDecision EvaluateSeedDecision(const SeedDecisionInput &in)
{
  SeedDecision out;
  out.priority_fifo =
      in.underfeet || (in.near_focus && in.pending_light_focus <= 20) ||
      (in.near_focus && in.can_seed);

  // Cruise: priority FIFO only. Sync RelightTerrainColumn under load was
  // 1–7s stream spikes (edge_R1). Ingress progress is enqueue + Capture (R5).
  if (in.moving_cruise)
  {
    if (in.near_focus || in.underfeet)
    {
      out.enqueue_pending = true;
      out.priority_fifo = true;
    }
    else
    {
      out.enqueue_pending = true;
    }
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
  // headroom (match pre-R1 gate). Unbounded near-focus Relight during load
  // hung edge (5+ min WorldLoad / hang_killed).
  if (in.near_focus && in.can_seed && in.frame_ms <= 20.0)
  {
    out.try_sync_seed = true;
    out.budget_ms = in.underfeet ? 3.0 : 2.0;
    return out;
  }

  // Cannot sync-seed: near-focus / underfeet → priority FIFO + PendingLight.
  if (in.near_focus || in.underfeet)
  {
    out.enqueue_pending = true;
    return out;
  }

  out.enqueue_pending = true;
  return out;
}

} // namespace cutum
