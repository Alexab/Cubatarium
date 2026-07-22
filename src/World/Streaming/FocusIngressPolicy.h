#pragma once

#include <algorithm>

namespace cutum
{

/// Pure focus-frontier ingress policy (P0): decide relight floor / promote /
/// whether sync hole-fill is allowed. No worldgen or SoftDefer rule changes —
/// only ownership wiring for cold holes (manual 134418: async=0 + pending).
struct FocusIngressInput
{
  bool moving{false};
  bool missing_mesh{false};
  int pending_focus{0};
  int mesh_async{0};
  double frame_ms{0.0};
};

struct FocusIngressDecision
{
  bool active{false};
  int relight_floor{0};
  bool promote_once{false};
  /// Sync RebuildChunkImmediate outside underfeet — false when cold pool so
  /// mesh_emerge cannot hitch 0.7–3s (prefer promote + async schedule).
  bool allow_sync_hole_fill{true};
};

inline FocusIngressDecision EvaluateFocusIngress(const FocusIngressInput &in)
{
  FocusIngressDecision out;
  if (!in.moving || !in.missing_mesh || in.pending_focus <= 0)
  {
    return out;
  }

  const bool cold_pool = in.mesh_async < 4;
  const bool warm_enough = in.frame_ms <= 50.0;
  if (!warm_enough && !cold_pool)
  {
    return out;
  }

  out.active = true;
  out.promote_once = warm_enough || cold_pool;

  // Capture() for a terrain column runs on the main thread inside Drain.
  // Old floors 36–48 made walk hitches unbounded when MeshEmerge also drained
  // (manual 194645: one Capture ≈ 3–4s). Pace: few enqueues/frame while moving;
  // idle Streaming floors elsewhere still catch up SoftDefer debt.
  if (cold_pool)
  {
    if (in.frame_ms <= 28.0)
    {
      out.relight_floor = in.pending_focus > 8 ? 4 : 2;
    }
    else
    {
      out.relight_floor = 1;
    }
  }
  else if (in.mesh_async < 8 && in.frame_ms <= 24.0)
  {
    out.relight_floor = in.pending_focus > 12 ? 3 : 2;
  }

  // Spike guard: cold async + hole → no non-underfeet sync fill.
  out.allow_sync_hole_fill = !cold_pool;
  return out;
}

/// Underfeet (horiz ≤ 1) always may sync-fill (V2a); otherwise follow policy.
inline bool AllowSyncHoleFillForColumn(const FocusIngressDecision &d,
                                       bool hole_underfeet)
{
  return hole_underfeet || d.allow_sync_hole_fill;
}

} // namespace cutum
