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
  /// Nearest focus miss Chebyshev horiz; <0 = unknown (skip K3 remesh band).
  int nearest_miss_horiz{-1};
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
    out.softdefer_requeue = holes ? 1 : 0;
    out.admit_batch = 1;
    out.allow_neighbor_dirty = false;
    out.starve_remesh_horiz = holes ? 1 : 2;
    out.promote_relight = light_debt ? 4 : (holes ? 2 : 0);
    // G2/H: moving holes FirstMesh headroom (was 2; G2→3; H→4 for rim miss_horiz).
    out.first_mesh_schedule = holes ? 4 : 1;
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
    // G3: Held→Dirty headroom under miss (not empty FirstMesh DirtyAdmit).
    if (holes)
    {
      out.softdefer_requeue = std::max(out.softdefer_requeue, 2);
    }
    out.admit_batch = 1;
    out.allow_neighbor_dirty = false;
    out.starve_remesh_horiz = 1;
    out.promote_relight = light_debt ? 4 : 2;
    // H: moving HoleDrain first_mesh 3→4 (manual 153832 miss_frac after G).
    out.first_mesh_schedule = 4;
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
  // Visual miss OR underfeet OR unfinished FOV debt (manual 160240 thrash:
  // UV=14 while visual_holes latch lagged → Normal/sch=12 with telem pending≥12).
  const bool holes = in.visual_holes || in.missing_underfeet ||
                     in.unfinished_visual >= 8;
  const size_t queued = MeshWorkQueuedApprox(in);
  const bool light_debt =
      holes && (in.pending_light_near >= 16 || in.unfinished_visual >= 8);

  MeshWorkAdmission::Mode mode = MeshWorkPickRawMode(in, holes);
  const int ring = std::max(1, in.ring_depth);
  const size_t queued_exit_cap =
      static_cast<size_t>(std::max(2, ring / 2));
  // J0: never Normal under holes/UV — even with cooled pending/queued FOV floor
  // sch=12 refill thrash (manual 170330 mid i=2: pend=1,mode=0,miss=1,uv=9).
  // Without holes: Warm when Queued refill risk (pending≥8 + queued>half-ring).
  if (mode == MeshWorkAdmission::Mode::Normal)
  {
    if (holes)
    {
      mode = MeshWorkAdmission::Mode::HoleDrain;
    }
    else if (queued > queued_exit_cap && in.pending_gpu >= 8)
    {
      mode = MeshWorkAdmission::Mode::WarmBacklog;
    }
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

  // J1: under HoleDrain/Deep miss backlog, give Finish more wall budget (Kick
  // bias is in ChunkMeshCache kick_cut/finish_cap — keep enqueue capped).
  // K2: pending≥16 deep backlog — slightly higher Finish wall share (0.85).
  if (holes &&
      (out.mode == MeshWorkAdmission::Mode::HoleDrain ||
       out.mode == MeshWorkAdmission::Mode::DeepBacklog))
  {
    if (in.pending_gpu >= 16)
    {
      out.gpu_budget_frac = std::max(out.gpu_budget_frac, 0.85);
    }
    else if (in.pending_gpu >= 12)
    {
      out.gpu_budget_frac = std::max(out.gpu_budget_frac, 0.82);
    }
  }

  if (light_debt && out.mode != MeshWorkAdmission::Mode::Normal)
  {
    // Manual 153832: UV≥8 crushed first_mesh to 3 and max_schedule to 3, nulling
    // G2 FM bump and starving rim while remesh keep_h=1 let stale explode.
    // Prefer FirstMesh slots; keep a small remesh band (horiz≤2) for stale.
    out.remesh_schedule = std::min(out.remesh_schedule, 1);
    out.starve_remesh_horiz = std::max(out.starve_remesh_horiz, 2);
    const int fm = std::max(0, out.first_mesh_schedule);
    const int need = fm + std::max(0, out.remesh_schedule);
    out.max_schedule = std::max(out.max_schedule, need);
    out.max_schedule = std::min(out.max_schedule, std::max(need, 5));
  }

  // K3: rim miss (mh 2–3) with cooled GPU pending — +1 remesh for stale/UV
  // without stealing FirstMesh slots (max_schedule covers FM+remesh).
  if (holes && in.pending_gpu <= 8 && in.nearest_miss_horiz >= 2 &&
      in.nearest_miss_horiz <= 3 &&
      (out.mode == MeshWorkAdmission::Mode::HoleDrain ||
       out.mode == MeshWorkAdmission::Mode::DeepBacklog))
  {
    out.remesh_schedule = std::max(0, out.remesh_schedule) + 1;
    out.starve_remesh_horiz = std::max(out.starve_remesh_horiz, 2);
    const int fm = std::max(0, out.first_mesh_schedule);
    out.max_schedule =
        std::max(out.max_schedule, fm + std::max(0, out.remesh_schedule));
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
