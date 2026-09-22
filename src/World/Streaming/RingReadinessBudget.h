#ifndef RING_READINESS_BUDGET_H
#define RING_READINESS_BUDGET_H

#include "World/Streaming/VisualStagePolicy.h"

#include <algorithm>
#include <cmath>

namespace cutum
{

/// A21 P7: feature flag — when false, EffectiveLitRing() returns baseline 4.
/// Default OFF: Evaluate is pure; do not make RingReadiness sole owner yet.
inline bool &RingReadinessBudgetEnabled()
{
  static bool enabled = false;
  return enabled;
}

/// A21 P7: late quality/admission controller (Evaluate only).
/// Wire behind feature flag; do not shrink SLA denominator by excluding rings.
/// Fog may coordinate quality transition but must NOT close holes/materials
/// inside the tested area (fog ≠ hole-close).
struct RingReadinessInputs
{
  int unfinished_visual{0};
  int fully_dark_stalled{0};
  int miss_horiz{0};
  int dirty_fm{0};
  double wall_ms{0.0};
  double backlog_age_ms{0.0};
  double arrival_rate{0.0};
  double service_rate{0.0};
  double memory_pressure{0.0};
  bool moving{false};
  int baseline_lit{4};
  int focus_radius{4};
  /// Hysteresis: previous frame effective ring + frames in current mode.
  int prev_effective_lit_ring{4};
  int frames_in_mode{0};
};

struct RingReadinessOutputs
{
  int effective_lit_ring{4};       // clamped [2..4]
  int effective_protect_ring{8};
  int dirty_admit_cap{8};
  int mesh_drain_cap{8};
  int near_load_ceiling{0};
  int ingress_shed_hint{0};
  float fog_pull_hint{0.0f}; // quality transition only — not hole-close
  bool degrade_active{false};
  int frames_in_mode{0};
  int min_frames_before_expand{30};
};

inline RingReadinessOutputs EvaluateRingReadinessBudget(
    const RingReadinessInputs &in)
{
  RingReadinessOutputs out;
  const int baseline = std::clamp(in.baseline_lit, 2, 4);
  int lit = baseline;
  const bool debt =
      in.unfinished_visual > 8 || in.fully_dark_stalled > 4 || in.miss_horiz > 0;
  const bool wall_hot = in.wall_ms > 20.0 || in.backlog_age_ms > 500.0;
  const bool under_capacity =
      in.service_rate > 0.0 && in.arrival_rate > in.service_rate * 1.2;

  if (debt && (wall_hot || under_capacity || in.memory_pressure > 0.85))
  {
    lit = std::max(2, baseline - 1);
    out.degrade_active = true;
  }
  if (debt && wall_hot && in.memory_pressure > 0.95)
  {
    lit = 2;
    out.degrade_active = true;
  }
  // Underfeet: never below 2.
  if (in.miss_horiz > 0 && in.focus_radius <= 2)
  {
    lit = std::max(lit, 2);
  }
  lit = std::clamp(lit, 2, 4);

  // Hysteresis: expand only after min frames in degrade; shrink immediately.
  const int prev = std::clamp(in.prev_effective_lit_ring, 2, 4);
  if (lit > prev && in.frames_in_mode < out.min_frames_before_expand)
  {
    lit = prev;
  }
  out.frames_in_mode =
      (lit == prev) ? in.frames_in_mode + 1 : 0;

  out.effective_lit_ring = lit;
  out.effective_protect_ring = lit + 4;
  out.dirty_admit_cap = out.degrade_active ? 4 : 8;
  out.mesh_drain_cap = out.degrade_active ? 4 : 8;
  out.near_load_ceiling = lit;
  out.ingress_shed_hint = out.degrade_active ? 1 : 0;
  out.fog_pull_hint = out.degrade_active ? 0.15f : 0.0f;
  return out;
}

/// Last Evaluate outputs for flag-gated readers (updated by streaming tick).
inline RingReadinessOutputs &RingReadinessLastOutputs()
{
  static RingReadinessOutputs last{};
  return last;
}

/// Read site helper: baseline lit ring unless flag ON.
inline int EffectiveLitRingOrBaseline(
    int baseline = kVisualStageLitDrawableHoriz)
{
  if (!RingReadinessBudgetEnabled())
  {
    return baseline;
  }
  return RingReadinessLastOutputs().effective_lit_ring;
}

} // namespace cutum

#endif
