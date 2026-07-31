#pragma once

#include <algorithm>

namespace cutum
{

/// Pure focus-frontier ingress policy (P0): decide relight floor / promote /
/// whether sync hole-fill is allowed. Stage SLA for rim latency (Era13 D).
struct FocusIngressInput
{
  bool moving{false};
  bool missing_mesh{false};
  int pending_focus{0};
  int mesh_async{0};
  double frame_ms{0.0};
  /// SoT unfinished visual count (held sample OK).
  int unfinished_visual{0};
  /// Stale-dark faces near camera (mesh dark, field lit) — remesh/light debt.
  int stale_dark_near{0};
};

struct FocusIngressDecision
{
  bool active{false};
  int relight_floor{0};
  bool promote_once{false};
  /// Sync RebuildChunkImmediate outside underfeet — false when cold pool so
  /// mesh_emerge cannot hitch 0.7–3s (prefer promote + async schedule).
  bool allow_sync_hole_fill{true};
  /// FirstMesh admit boost while frontier Stage SLA is active.
  int first_mesh_admit{0};
};

inline FocusIngressDecision EvaluateFocusIngress(const FocusIngressInput &in)
{
  FocusIngressDecision out;
  // Frontier active: classic missing+pending, OR SoT unfinished, OR stale-dark
  // remesh debt near camera (manual 225337 rim wait with uv≈0 / dark high).
  const bool classic =
      in.moving && in.missing_mesh && in.pending_focus > 0;
  const bool sot_frontier =
      in.moving && (in.unfinished_visual > 0 || in.missing_mesh);
  const bool stale_frontier =
      in.moving && in.stale_dark_near > 8 &&
      (in.pending_focus > 0 || in.unfinished_visual > 0 || in.missing_mesh);
  if (!classic && !sot_frontier && !stale_frontier)
  {
    return out;
  }

  const bool cold_pool = in.mesh_async < 4;
  const bool warm_enough = in.frame_ms <= 50.0;
  if (!warm_enough && !cold_pool && !in.missing_mesh)
  {
    return out;
  }

  out.active = true;
  out.promote_once = warm_enough || cold_pool || in.missing_mesh;
  out.first_mesh_admit =
      in.missing_mesh ? (cold_pool ? 4 : 2)
                      : (in.unfinished_visual > 0 ? 2 : 0);

  // Rim FirstMesh SLA: missing + pending backlog → prefer admit over Capture
  // (manual 131234 p09–12: pend=19 / emerge=102 while miss stuck).
  const bool rim_first_mesh_sla =
      in.missing_mesh && in.pending_focus > 4;
  if (rim_first_mesh_sla)
  {
    out.first_mesh_admit =
        std::max(out.first_mesh_admit, cold_pool ? 5 : 3);
  }

  // Capture() for a terrain column runs on the main thread inside Drain.
  // Pace: few enqueues/frame while moving; SoftDefer Capture floor elsewhere.
  if (cold_pool)
  {
    if (in.frame_ms <= 28.0)
    {
      out.relight_floor = in.pending_focus > 8 ? 6 : 3;
    }
    else
    {
      out.relight_floor = 2;
    }
  }
  else if (in.mesh_async < 8 && in.frame_ms <= 24.0)
  {
    out.relight_floor = in.pending_focus > 12 ? 3 : 2;
  }
  else if (stale_frontier && in.pending_focus > 0)
  {
    out.relight_floor = std::max(out.relight_floor, 2);
  }
  // Cap Capture floor while FirstMesh is starving on the rim.
  if (rim_first_mesh_sla && out.relight_floor > 0)
  {
    out.relight_floor = std::min(out.relight_floor, cold_pool ? 3 : 2);
  }

  // Spike guard: cold async + hole → no non-underfeet sync fill — except when
  // missing mesh (land rim: cold_pool alone left miss_stuck 16–24s).
  out.allow_sync_hole_fill = !cold_pool || in.missing_mesh;
  return out;
}

/// Underfeet (horiz ≤ 1) always may sync-fill (V2a); otherwise follow policy.
inline bool AllowSyncHoleFillForColumn(const FocusIngressDecision &d,
                                       bool hole_underfeet)
{
  return hole_underfeet || d.allow_sync_hole_fill;
}

} // namespace cutum
