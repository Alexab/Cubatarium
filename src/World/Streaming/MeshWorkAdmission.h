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
  /// Previous frame mode for HoleDrain hysteresis (SoT 100351 Normal thrash).
  uint8_t prev_mode{0};
  /// GPU readback ring depth (kReadbackRing); used for enqueue_gpu_budget.
  int ring_depth{8};
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
  /// Under holes: guaranteed FirstMesh slots (Pass 1); remesh uses remesh_schedule.
  int first_mesh_schedule{0};
  int remesh_schedule{0};
  /// Max new Queued GPU applies this frame (Apply enqueue throttle).
  int enqueue_gpu_budget{4};
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

inline void MeshWorkFillModeDefaults(MeshWorkAdmission &out,
                                     MeshWorkAdmission::Mode mode,
                                     const MeshWorkAdmissionInput &in,
                                     size_t queued, bool holes, bool light_debt)
{
  out.mode = mode;
  const int ring = std::max(1, in.ring_depth);
  switch (mode)
  {
  case MeshWorkAdmission::Mode::DeepBacklog:
    out.max_schedule = 2;
    out.max_drain = 16;
    out.gpu_apply_max = std::max(24, ring * 2);
    out.gpu_budget_frac = 0.85;
    out.dirty_admit_budget = 1;
    out.softdefer_requeue = 0;
    out.admit_batch = 1;
    out.allow_neighbor_dirty = false;
    out.starve_remesh_horiz = holes ? 1 : 2;
    out.promote_relight = light_debt ? 4 : (holes ? 2 : 0);
    out.first_mesh_schedule = holes ? (in.moving ? 2 : 3) : 1;
    out.remesh_schedule = holes ? 0 : 1;
    break;
  case MeshWorkAdmission::Mode::HoleDrain:
    out.max_schedule = in.pending_gpu >= 16 ? 2 : 4;
    out.max_drain = 12;
    out.gpu_apply_max = std::max(16, ring * 2);
    out.gpu_budget_frac = 0.75;
    out.dirty_admit_budget =
        std::max(0, 4 - static_cast<int>(std::min<size_t>(queued, 4)));
    out.softdefer_requeue = out.dirty_admit_budget > 0 ? 1 : 0;
    out.admit_batch = 1;
    out.allow_neighbor_dirty = false;
    out.starve_remesh_horiz = 1;
    out.promote_relight = light_debt ? 4 : 2;
    out.first_mesh_schedule = in.moving ? 2 : 3;
    out.remesh_schedule = 1;
    if (!in.moving)
    {
      out.max_schedule = std::max(out.max_schedule, 6);
      out.admit_batch = 2;
      out.dirty_admit_budget = std::max(out.dirty_admit_budget, 2);
      out.softdefer_requeue = std::max(out.softdefer_requeue, 1);
      out.first_mesh_schedule = std::max(out.first_mesh_schedule, 6);
      out.remesh_schedule = std::max(out.remesh_schedule, 1);
    }
    break;
  case MeshWorkAdmission::Mode::WarmBacklog:
    out.max_schedule = 6;
    out.max_drain = 12;
    out.gpu_apply_max = std::max(16, ring * 2);
    out.gpu_budget_frac = 0.75;
    out.dirty_admit_budget = 4;
    out.softdefer_requeue = 2;
    out.admit_batch = 2;
    out.allow_neighbor_dirty = true;
    out.starve_remesh_horiz = 2;
    out.promote_relight = in.pending_light_near >= 16 ? 2 : 0;
    out.first_mesh_schedule = holes ? 2 : 3;
    out.remesh_schedule = 3;
    break;
  case MeshWorkAdmission::Mode::Normal:
  default:
    out.max_schedule = 0;
    out.max_drain = 12;
    out.gpu_apply_max = std::max(4, ring);
    out.gpu_budget_frac = holes ? 0.6 : 0.5;
    out.dirty_admit_budget = 8;
    out.softdefer_requeue = 4;
    out.admit_batch = holes ? (in.moving ? 3 : 4) : 4;
    out.allow_neighbor_dirty = true;
    out.starve_remesh_horiz = holes ? 2 : 3;
    out.promote_relight = 0;
    out.first_mesh_schedule = 0; // unused when Normal (full schedule)
    out.remesh_schedule = 0;
    break;
  }
  const int kicked = static_cast<int>(std::min<size_t>(in.pending_gpu_kicked, 64));
  out.enqueue_gpu_budget =
      std::max(0, ring - kicked + (mode == MeshWorkAdmission::Mode::Normal ? 2 : 0));
  if (mode == MeshWorkAdmission::Mode::HoleDrain ||
      mode == MeshWorkAdmission::Mode::DeepBacklog)
  {
    // Prefer Finish: do not refill Queued beyond one ring of headroom.
    out.enqueue_gpu_budget = std::max(0, ring - kicked);
  }
  if (out.first_mesh_schedule > 0 && out.max_schedule > 0)
  {
    out.max_schedule =
        std::max(out.max_schedule, out.first_mesh_schedule + out.remesh_schedule);
  }
}

inline MeshWorkAdmission::Mode
MeshWorkPickRawMode(const MeshWorkAdmissionInput &in, bool holes)
{
  if (in.pending_gpu >= 24)
  {
    return MeshWorkAdmission::Mode::DeepBacklog;
  }
  if (in.pending_gpu >= 12 && holes)
  {
    return MeshWorkAdmission::Mode::HoleDrain;
  }
  if (in.pending_gpu >= 12)
  {
    return MeshWorkAdmission::Mode::WarmBacklog;
  }
  return MeshWorkAdmission::Mode::Normal;
}

inline MeshWorkAdmission
ComputeMeshWorkAdmission(const MeshWorkAdmissionInput &in)
{
  MeshWorkAdmission out;
  const bool holes = in.visual_holes || in.missing_underfeet;
  const size_t queued = MeshWorkQueuedApprox(in);
  const bool light_debt =
      holes && (in.pending_light_near >= 16 || in.unfinished_visual >= 8);

  MeshWorkAdmission::Mode mode = MeshWorkPickRawMode(in, holes);
  const int ring = std::max(1, in.ring_depth);
  const size_t queued_exit_cap =
      static_cast<size_t>(std::max(2, ring / 2));
  // G0: after drain-first, pending can dip below 12 while Queued still fills
  // the ring — never Normal under holes (SoT 120321 mode=0/sch=20 thrash).
  if (holes && queued >= static_cast<size_t>(ring) &&
      mode == MeshWorkAdmission::Mode::Normal)
  {
    mode = MeshWorkAdmission::Mode::HoleDrain;
  }
  const auto prev = static_cast<MeshWorkAdmission::Mode>(in.prev_mode);
  const bool was_hole_backlog =
      prev == MeshWorkAdmission::Mode::HoleDrain ||
      prev == MeshWorkAdmission::Mode::DeepBacklog;
  // Exit HoleDrain/Deep only when holes cleared, pending cooled (≤8), and
  // Queued drained below half-ring (avoid Immediate Normal refill).
  if (was_hole_backlog)
  {
    const bool can_exit =
        !holes && in.pending_gpu <= 8 && queued <= queued_exit_cap;
    if (!can_exit)
    {
      if (in.pending_gpu >= 24)
      {
        mode = MeshWorkAdmission::Mode::DeepBacklog;
      }
      else if (holes || in.pending_gpu > 8)
      {
        mode = holes ? MeshWorkAdmission::Mode::HoleDrain
                     : MeshWorkAdmission::Mode::WarmBacklog;
      }
      else
      {
        // Pending cooled but Queued still high — stay Warm, not Normal.
        mode = MeshWorkAdmission::Mode::WarmBacklog;
      }
    }
  }

  MeshWorkFillModeDefaults(out, mode, in, queued, holes, light_debt);

  if (light_debt && out.mode != MeshWorkAdmission::Mode::Normal)
  {
    out.max_schedule = std::min(out.max_schedule, 3);
    out.first_mesh_schedule = std::min(out.first_mesh_schedule, 3);
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
