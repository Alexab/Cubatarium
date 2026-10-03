#pragma once
// BUDGET_MS: 0.0  // perf-root P4: measure via Tracy; kill-switch required for new heuristics

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
  /// Era34 P1: SoftDefer empty placeholders in focus (FirstMesh admit floor).
  int soft_defer_empty_n{0};
  /// Dark void faces near camera — frontier gen/light imbalance.
  int void_near{0};
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
  // Era20: miss activates even when !moving (idle/stop miss stuck 214034).
  const bool classic = in.missing_mesh && in.pending_focus > 0;
  const bool sot_frontier =
      in.missing_mesh || (in.moving && in.unfinished_visual > 0) ||
      (in.moving && in.soft_defer_empty_n > 0);
  const bool stale_frontier =
      (in.moving || in.missing_mesh) && in.stale_dark_near > 8 &&
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
  // Era32 P3: empty∨miss FOV — guarantee ≥1 FirstMesh admit/frame
  // (dual-debt must not starve HP FirstMesh under Relight pressure).
  // Era34 P1: SoftDefer empty also floors FirstMesh admit.
  if (in.missing_mesh || in.unfinished_visual > 0 || in.soft_defer_empty_n > 0)
  {
    out.first_mesh_admit = std::max(out.first_mesh_admit, 1);
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

  // Frontier admission: gen must not outpace relight when void debt is high.
  if (in.moving && in.void_near > 200 && in.pending_focus > 24)
  {
    out.first_mesh_admit = std::min(out.first_mesh_admit, 1);
    out.relight_floor =
        std::max(out.relight_floor, in.pending_focus > 48 ? 4 : 3);
  }
  return out;
}

/// Underfeet (horiz ≤ 1) always may sync-fill (V2a); otherwise follow policy.
inline bool AllowSyncHoleFillForColumn(const FocusIngressDecision &d,
                                       bool hole_underfeet)
{
  return hole_underfeet || d.allow_sync_hole_fill;
}

/// Cruise time-budget B: never RebuildChunkImmediate while moving (Luanti:
/// mesh off the main thread). Idle Imm still gated on pending light.
/// P3: idle Imm also backs off when GPU apply queue or FIFO is already full.
inline bool ShouldAllowImmediateMesh(bool moving, bool pending_light,
                                     int pending_gpu_queued = 0, int fifo_n = 0,
                                     int fifo_soft_cap = 0,
                                     bool visual_holes = false)
{
  if (moving || pending_light)
  {
    return false;
  }
  if (pending_gpu_queued >= 32)
  {
    return false;
  }
  if (fifo_soft_cap > 0 && fifo_n >= fifo_soft_cap)
  {
    return false;
  }
  if (visual_holes && fifo_soft_cap > 0 && fifo_n >= (fifo_soft_cap * 3) / 4)
  {
    return false;
  }
  return true;
}

} // namespace cutum
