#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace cutum
{

/// Single authority for how much mesh work may *start* this frame given GPU
/// apply backlog and light debt. Floors may propose schedule/drain; Finalize*
/// is the only hard cap. SoftDeferHeld / Admit / neighbor Dirty read the same
/// quotas (no scattered if pending>=12).
struct MeshWorkAdmissionInput
{
  size_t pending_gpu{0};
  size_t pending_gpu_queued{0};
  size_t pending_gpu_kicked{0};
  bool visual_holes{false};
  bool missing_underfeet{false};
  bool moving{false};
  int pending_light_near{0};
  int unfinished_visual{0};
};

struct MeshWorkAdmission
{
  enum class Mode : uint8_t
  {
    Normal = 0,
    WarmBacklog = 1,
    DeepBacklog = 2,
    HoleDrain = 3,
  };

  int max_schedule{0}; // hard cap; ignored when mode==Normal (Finalize passthrough)
  int max_drain{12};
  int gpu_apply_max{4};
  double gpu_budget_frac{0.5};
  int dirty_admit_budget{8};
  int softdefer_requeue{4};
  int admit_batch{4};
  bool allow_neighbor_dirty{true};
  int promote_relight{0};
  int starve_remesh_horiz{2};
  Mode mode{Mode::Normal};
};

inline size_t MeshWorkQueuedApprox(const MeshWorkAdmissionInput &in)
{
  if (in.pending_gpu_queued > 0 || in.pending_gpu_kicked > 0)
  {
    return in.pending_gpu_queued;
  }
  if (in.pending_gpu_kicked >= in.pending_gpu)
  {
    return 0;
  }
  return in.pending_gpu - in.pending_gpu_kicked;
}

inline MeshWorkAdmission
ComputeMeshWorkAdmission(const MeshWorkAdmissionInput &in)
{
  MeshWorkAdmission out;
  const bool holes = in.visual_holes || in.missing_underfeet;
  const size_t queued = MeshWorkQueuedApprox(in);
  const bool light_debt =
      holes && (in.pending_light_near >= 16 || in.unfinished_visual >= 8);

  if (in.pending_gpu >= 24)
  {
    out.mode = MeshWorkAdmission::Mode::DeepBacklog;
    out.max_schedule = 2;
    out.max_drain = 16;
    out.gpu_apply_max = 24;
    out.gpu_budget_frac = 0.85;
    out.dirty_admit_budget = 1;
    out.softdefer_requeue = 0;
    out.admit_batch = 1;
    out.allow_neighbor_dirty = false;
    out.starve_remesh_horiz = holes ? 1 : 2;
    out.promote_relight = light_debt ? 4 : (holes ? 2 : 0);
  }
  else if (in.pending_gpu >= 12 && holes)
  {
    out.mode = MeshWorkAdmission::Mode::HoleDrain;
    out.max_schedule = in.pending_gpu >= 16 ? 2 : 4;
    out.max_drain = 12;
    out.gpu_apply_max = 16;
    out.gpu_budget_frac = 0.75;
    out.dirty_admit_budget =
        std::max(0, 4 - static_cast<int>(std::min<size_t>(queued, 4)));
    out.softdefer_requeue = out.dirty_admit_budget > 0 ? 1 : 0;
    out.admit_batch = 1;
    out.allow_neighbor_dirty = false;
    out.starve_remesh_horiz = 1;
    out.promote_relight = light_debt ? 4 : 2;
  }
  else if (in.pending_gpu >= 12)
  {
    out.mode = MeshWorkAdmission::Mode::WarmBacklog;
    out.max_schedule = 6;
    out.max_drain = 12;
    out.gpu_apply_max = 16;
    out.gpu_budget_frac = 0.75;
    out.dirty_admit_budget = 4;
    out.softdefer_requeue = 2;
    out.admit_batch = 2;
    out.allow_neighbor_dirty = true;
    out.starve_remesh_horiz = 2;
    out.promote_relight = in.pending_light_near >= 16 ? 2 : 0;
  }
  else
  {
    out.mode = MeshWorkAdmission::Mode::Normal;
    out.max_schedule = 0; // Finalize ignores
    out.max_drain = 12;
    out.gpu_apply_max = 4;
    out.gpu_budget_frac = holes ? 0.6 : 0.5;
    out.dirty_admit_budget = 8;
    out.softdefer_requeue = 4;
    out.admit_batch = holes ? (in.moving ? 3 : 4) : 4;
    out.allow_neighbor_dirty = true;
    out.starve_remesh_horiz = holes ? 2 : 3;
    out.promote_relight = 0;
  }

  if (light_debt && out.mode != MeshWorkAdmission::Mode::Normal)
  {
    out.max_schedule = std::min(out.max_schedule, 3);
    out.starve_remesh_horiz = 1;
  }
  return out;
}

inline int FinalizeSchedule(int proposed_schedule, const MeshWorkAdmission &adm)
{
  proposed_schedule = std::max(0, proposed_schedule);
  if (adm.mode == MeshWorkAdmission::Mode::Normal)
  {
    return proposed_schedule;
  }
  return std::min(proposed_schedule, std::max(0, adm.max_schedule));
}

inline int FinalizeDrain(int proposed_drain, const MeshWorkAdmission &adm)
{
  proposed_drain = std::max(0, proposed_drain);
  if (adm.mode == MeshWorkAdmission::Mode::Normal)
  {
    return proposed_drain;
  }
  // Backlog modes: never starve Apply below admission floor.
  return std::max(proposed_drain, adm.max_drain);
}

} // namespace cutum
