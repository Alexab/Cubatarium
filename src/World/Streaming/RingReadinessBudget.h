#ifndef RING_READINESS_BUDGET_H
#define RING_READINESS_BUDGET_H

#include <algorithm>
#include <cmath>

namespace cutum
{

/// A21 P7: late quality/admission controller (Evaluate only).
/// Wire behind feature flag; do not shrink SLA denominator by excluding rings.
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
};

struct RingReadinessOutputs
{
  int effective_lit_ring{4};
  int effective_protect_ring{8};
  int dirty_admit_cap{8};
  int mesh_drain_cap{8};
  float fog_pull_hint{0.0f};
  bool degrade_active{false};
};

inline RingReadinessOutputs EvaluateRingReadinessBudget(
    const RingReadinessInputs &in)
{
  RingReadinessOutputs out;
  const int baseline = std::max(2, in.baseline_lit);
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

  out.effective_lit_ring = lit;
  out.effective_protect_ring = lit + 4;
  out.dirty_admit_cap = out.degrade_active ? 4 : 8;
  out.mesh_drain_cap = out.degrade_active ? 4 : 8;
  out.fog_pull_hint = out.degrade_active ? 0.15f : 0.0f;
  // Fog may coordinate quality transition but must NOT close holes/materials
  // inside the tested area (A21 P7.3).
  return out;
}

} // namespace cutum

#endif
