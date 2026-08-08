#pragma once

#include <algorithm>

namespace cutum
{

/// Era19: single per-frame streaming budget SoT (Cubyz/UE-style time slice).
/// SoftDeferCapture + VB bg floors must go through Evaluate — not ad-hoc max().
struct FrameStreamingBudgetInput
{
  double frame_ms{0.0};
  double bad_frame_ms{24.0};
  bool missing_visible_mesh{false};
  int unfinished{0};
  int visible_black_n{0};
  int pending_light_focus_n{0};
  bool moving{false};
  /// Kill-switch: Era18 SoftDefer Capture floor while VB (bisect).
  bool era18_vb_capture_floor{true};
  /// Kill-switch: Era18 bg_budget floor while VB (bisect).
  bool era18_vb_bg_budget_floor{true};
  /// Era19 P1: miss-first — shrink Capture/VB heal on hitch; FirstMesh under miss.
  bool miss_first_budget{false};
};

struct FrameStreamingBudgetDecision
{
  /// SoftDefer ColumnFlow Capture/Drain floor this frame (0 = none).
  int soft_defer_capture_budget{0};
  /// When true, SoftDefer ticket kind must be FirstMesh (not RelightThenMesh).
  bool capture_first_mesh_only{false};
  /// If apply_vb_bg_floor, bg_budget = max(bg_budget, vb_bg_budget_floor).
  bool apply_vb_bg_floor{false};
  int vb_bg_budget_floor{0};
  /// Soft ms SoT for telem (bad_frame_ms).
  int frame_budget_ms{0};
  /// Skipped/shrunk VB heal because miss/hitch (I-B1/I-B2).
  bool heal_deferred_for_miss{false};
  /// Would have spent Capture floor on an already-hot frame.
  bool capture_over_budget{false};
};

/// Hitch drain floor: bump only FirstMesh / no_ticket — never VB Capture storm.
inline int EvaluateMissFirstDrainN(int recover_n, bool missing_visible_mesh,
                                   bool visible_black_no_ticket, bool visible_black,
                                   bool hitch, bool moving, bool miss_first)
{
  int drain_n = recover_n;
  if (missing_visible_mesh || visible_black_no_ticket)
  {
    drain_n = std::max(drain_n, moving ? 6 : 10);
    return drain_n;
  }
  if (!visible_black)
  {
    return drain_n;
  }
  if (miss_first)
  {
    // Calm VB only — hitch does not raise Relight/Remesh storm.
    if (!hitch)
    {
      drain_n = std::max(drain_n, moving ? 4 : 8);
    }
    return drain_n;
  }
  // Legacy Era18: hitch orphans keep a floor even when wall is hot.
  const int vb_floor = moving ? 4 : 8;
  const int hitch_floor = hitch ? (moving ? 6 : 10) : vb_floor;
  return std::max(drain_n, hitch_floor);
}

inline FrameStreamingBudgetDecision EvaluateFrameStreamingBudget(
    const FrameStreamingBudgetInput &in)
{
  FrameStreamingBudgetDecision out;
  out.frame_budget_ms = static_cast<int>(in.bad_frame_ms);
  const bool hitch = in.frame_ms > in.bad_frame_ms;
  const bool miss = in.missing_visible_mesh;
  const bool unfinished = in.unfinished > 0;

  if (in.miss_first_budget)
  {
    // I-B2: under miss — Capture ≤1, FirstMesh only; VB Relight Capture off.
    if (miss)
    {
      out.soft_defer_capture_budget = 1;
      out.capture_first_mesh_only = true;
      if (hitch || in.visible_black_n > 0)
      {
        out.heal_deferred_for_miss = true;
      }
      // Calm idle: keep light mid-floor 1 when pending_focus (risk mitigation).
      if (!hitch && in.pending_light_focus_n > 0 &&
          in.era18_vb_bg_budget_floor)
      {
        out.apply_vb_bg_floor = true;
        out.vb_bg_budget_floor = 1;
      }
    }
    else if (unfinished && in.pending_light_focus_n > 0)
    {
      int floor_budget =
          hitch ? std::min(4, 1 + in.unfinished / 8)
                : std::min(6, 2 + in.unfinished / 6);
      // Hitch: shrink Capture first (never starve FirstMesh path separately).
      if (hitch)
      {
        floor_budget = std::min(floor_budget, 1);
        out.capture_over_budget = floor_budget > 0;
      }
      out.soft_defer_capture_budget = floor_budget;
    }
    else if (in.visible_black_n > 0 && in.era18_vb_capture_floor)
    {
      if (hitch)
      {
        // I-B1: shrink Capture/VB heal on hot wall.
        out.soft_defer_capture_budget = 0;
        out.heal_deferred_for_miss = true;
      }
      else
      {
        out.soft_defer_capture_budget = in.moving ? 1 : 2;
      }
    }

    // VB bg_budget floor — off on hitch; calm keep when VB or pending_focus.
    if (in.era18_vb_bg_budget_floor && in.visible_black_n > 0 && !miss)
    {
      if (hitch)
      {
        out.apply_vb_bg_floor = false;
        out.vb_bg_budget_floor = 0;
        out.heal_deferred_for_miss = true;
      }
      else
      {
        out.apply_vb_bg_floor = true;
        out.vb_bg_budget_floor = in.moving ? 1 : 2;
      }
    }
  }
  else
  {
    // Era18 legacy floors (P0 harness / bisect with kill-switches).
    if (miss)
    {
      out.soft_defer_capture_budget = in.moving ? 1 : 2;
      out.capture_first_mesh_only = true;
    }
    else if (unfinished && in.pending_light_focus_n > 0)
    {
      out.soft_defer_capture_budget =
          hitch ? std::min(4, 1 + in.unfinished / 8)
                : std::min(6, 2 + in.unfinished / 6);
    }
    else if (in.visible_black_n > 0 && in.era18_vb_capture_floor)
    {
      out.soft_defer_capture_budget = in.moving ? 1 : 2;
    }

    if (in.era18_vb_bg_budget_floor &&
        (in.visible_black_n > 0 /* dark_face gated by caller */))
    {
      out.apply_vb_bg_floor = true;
      out.vb_bg_budget_floor = hitch ? 1 : 2;
    }

    if (hitch && out.soft_defer_capture_budget > 0 &&
        in.visible_black_n > 0 && miss)
    {
      out.capture_over_budget = true;
    }
  }

  return out;
}

} // namespace cutum
