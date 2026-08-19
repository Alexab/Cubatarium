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

  // Era14 TD-ARCH-044: widen cheap commit seed so PendingLight trail shrinks.
  // Visual holes: allow slightly hotter frame budget for near sync seed.
  const double cruise_seed_frame_cap =
      in.visual_holes > 0 ? 40.0 : 32.0;
  if (in.moving_cruise)
  {
    // V3 optional: cheap sync seed under high pending_light to shrink FIFO trail.
    if (in.can_seed && in.near_focus && in.pending_light_focus > 24 &&
        in.frame_ms <= cruise_seed_frame_cap + 6.0)
    {
      out.try_sync_seed = true;
      out.cheap_seed = true;
      out.budget_ms = 2.0;
      return out;
    }
    if (in.can_seed && (in.underfeet || in.near_focus) &&
        in.frame_ms <= cruise_seed_frame_cap)
    {
      out.try_sync_seed = true;
      out.cheap_seed = true;
      out.budget_ms = in.underfeet ? 2.0 : 2.5;
      return out;
    }
    out.enqueue_pending = true;
    out.priority_fifo = in.near_focus || in.underfeet;
    return out;
  }

  // Idle underfeet: prefer sync seed even with mild hitch / holes (F2 cold).
  if (in.underfeet && in.can_seed && in.frame_ms <= 28.0)
  {
    out.try_sync_seed = true;
    out.budget_ms = 3.5;
    return out;
  }

  // Idle near-focus with neighborhood.
  if (in.near_focus && in.can_seed && in.frame_ms <= 28.0)
  {
    out.try_sync_seed = true;
    out.budget_ms = in.underfeet ? 3.5 : 2.5;
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
