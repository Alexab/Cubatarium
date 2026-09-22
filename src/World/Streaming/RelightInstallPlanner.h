#pragma once

#include "World/Streaming/ColumnEmergeState.h"
#include "World/Streaming/ColumnVisualState.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Streaming/MeshLightStalePolicy.h"
#include "World/Streaming/RelightFifoPolicy.h"

#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace cutum
{

/// FZ2.7-B2: install path classification for MarkRelit refactor.
enum class ColumnInstallPath : uint8_t
{
  Skip = 0,
  PartialNoDirty,
  PrimaryConsume,
  PrimaryDefer,
  PrimaryEnter,
  PrimaryQuiesce,
  PrimaryStandard,
  NeighborSeam,
  OrphanGround,
};

struct LitApplyYBand
{
  int min_y{0};
  int max_y{0};
};

struct ColumnChunkSnapshot
{
  glm::ivec3 coord{0};
  bool has_greedy{false};
  bool has_drawable{false};
  bool is_dirty{false};
  bool raa_pending{false};
  bool gpu_pending{false};
  bool inflight{false};
  bool fully_dark{false};
  bool soft_defer{false};
  bool still_stale{false};
  bool has_publish_progress{false};
  int prefer_kick_stall_frames{0};
  ColumnVisualState visual{ColumnVisualState::Ready};
  uint64_t meshed_light_rev{0};
  uint64_t light_field_rev{0};
};

struct LitApplyColumnInput
{
  glm::ivec2 column{0};
  bool is_primary{false};
  bool finalize_gate{true};
  bool primary_only{false};
  bool consume_mode{false};
  bool defer_side{false};
  bool enter_gate{false};
  bool enter_quiesce{false};
  bool suppress_relight_seam{false};
  bool priority_mesh{true};
  bool moving{false};
  int focus_horiz{999};
  bool has_fm_ticket{false};
  bool column_has_drawable{false};
  bool soft_defer_empty_owned{false};
  bool damp_soft_empty_remesh{false};
  bool had_mesh{false};
  bool any_drawable{false};
  bool has_repair_ticket{false};
  bool column_settled{false};
  bool sticky_owned{false};
  bool force_stale_ticket{false};
  LitApplyYBand lit_band{};
  LitApplyYBand dirty_band{};
  std::vector<ColumnChunkSnapshot> relit_chunks;
};

struct LitApplyPlan
{
  ColumnInstallPath path{ColumnInstallPath::Skip};
  std::vector<glm::ivec3> mark_dirty;
  std::vector<glm::ivec3> mark_dirty_priority;
  std::vector<glm::ivec3> prefer_kick_gpu;
  std::vector<glm::ivec3> request_raa;
  bool note_prefer_kick_stall{false};
  bool enqueue_first_mesh{false};
  glm::ivec2 first_mesh_column{0};
  ColumnEmergeState fsm_after{ColumnEmergeState::LitReady};
  bool persistence_light_complete{false};
  bool erase_pending_light{false};
  bool erase_inflight{false};
  bool skip_neighbor{false};
  int schedule_n{0};
  int skip_already_dirty_n{0};
  int skip_inflight_n{0};
  int suppress_enter_settled_n{0};
};

inline ColumnInstallPath ClassifyColumnInstallPath(
    const LitApplyColumnInput &in)
{
  if (!in.finalize_gate)
  {
    return ColumnInstallPath::PartialNoDirty;
  }
  if (in.is_primary)
  {
    if (in.consume_mode && in.primary_only)
    {
      return ColumnInstallPath::PrimaryConsume;
    }
    if (ShouldUsePrimarySlimInstallPath(in.primary_only, in.enter_gate,
                                        in.enter_quiesce))
    {
      return ColumnInstallPath::PrimaryConsume;
    }
    // FZ2.7-B3: enter primary_only → Consume planner (no RemeshSeam enqueue).
    if (ShouldUseEnterSlimInstallPath(in.enter_gate, in.enter_quiesce,
                                      in.primary_only))
    {
      return ColumnInstallPath::PrimaryConsume;
    }
    if (in.primary_only && in.defer_side)
    {
      return ColumnInstallPath::PrimaryDefer;
    }
    if (in.enter_quiesce)
    {
      return ColumnInstallPath::PrimaryQuiesce;
    }
    if (in.enter_gate)
    {
      return ColumnInstallPath::PrimaryEnter;
    }
    return ColumnInstallPath::PrimaryStandard;
  }
  if (in.primary_only)
  {
    return ColumnInstallPath::Skip;
  }
  return ColumnInstallPath::NeighborSeam;
}

inline bool ShouldScheduleChunkRemesh(
    const ColumnChunkSnapshot &chunk, bool enter_quiesce,
    bool column_settled, bool sticky_owned, bool force_stale,
    bool light_or_voxel_delta, RemeshAfterLitApplyDecision *out_decision)
{
  const auto decision = ClassifyRemeshAfterLitApply(
      chunk.is_dirty, chunk.raa_pending, chunk.gpu_pending, chunk.inflight,
      enter_quiesce, chunk.fully_dark, /*column_visual_ready=*/false,
      light_or_voxel_delta, chunk.still_stale);
  if (out_decision)
  {
    *out_decision = decision;
  }
  if (decision == RemeshAfterLitApplyDecision::Schedule)
  {
    if (enter_quiesce && column_settled && !sticky_owned && !force_stale)
    {
      return false;
    }
    return true;
  }
  if (decision == RemeshAfterLitApplyDecision::PreferKickGpu)
  {
    return false;
  }
  return false;
}

/// Light field moved past the baked mesh — remesh can pick up new bytes.
inline bool ChunkLightRevAhead(const ColumnChunkSnapshot &chunk)
{
  return IsMeshLightStale(chunk.meshed_light_rev, chunk.light_field_rev);
}

/// FZ2.7-P7 / A21-07: do not remesh FullyDark when light rev matches (valid
/// dark cave). `still_stale` (dark census) is observational — not a remesh
/// trigger by itself. force_stale_ticket still forces remesh.
/// Missing mesh still needs FirstMesh.
inline bool ShouldRemeshAfterLitApplyForHole(const ColumnChunkSnapshot &chunk,
                                             bool force_stale_ticket)
{
  if (!chunk.has_drawable)
  {
    return true;
  }
  if (ChunkLightRevAhead(chunk))
  {
    return true;
  }
  if (chunk.fully_dark && force_stale_ticket)
  {
    return true;
  }
  // A21 residual: equal-rev FullyDark with Dirty = repair demand (not LegalDark).
  // Legal cave never sits is_dirty; remesh so MarkDirty/PreferKick paths run.
  if (chunk.fully_dark && chunk.is_dirty)
  {
    return true;
  }
  (void)chunk.still_stale; // census-only; LightValidity ≠ dark vertices
  return false;
}

/// FZ2.7: already-Dirty missing mesh / FullyDark-with-light-delta bump Q head.
/// G1: consume_mode bumps FullyDark holes — except A21 P4 valid dark (revs
/// match / !light_rev_ahead): equal-rev FullyDark is not a remesh trigger.
inline bool ShouldBumpDirtyHeadForVisualHole(bool is_dirty, bool fully_dark,
                                             bool has_drawable, int focus_horiz,
                                             bool consume_mode,
                                             bool light_rev_ahead = true,
                                             int ring = RelightFifoTrimProtectHoriz())
{
  if (!is_dirty)
  {
    return false;
  }
  if (has_drawable && !fully_dark)
  {
    return false;
  }
  // Valid FullyDark (matching light rev): observational dark, not demand.
  if (has_drawable && fully_dark && !light_rev_ahead)
  {
    return false;
  }
  if (consume_mode)
  {
    return true;
  }
  return focus_horiz >= 0 && focus_horiz <= ring;
}

/// FZ2.7-P12 A1: skip_already_dirty on a hole must still force FirstMesh class.
inline bool ShouldForceFirstMeshOnSkipAlreadyDirty(bool is_dirty,
                                                   bool has_drawable,
                                                   bool has_greedy,
                                                   bool soft_defer_empty_or_held)
{
  if (!is_dirty || has_drawable)
  {
    return false;
  }
  return !has_greedy || soft_defer_empty_or_held;
}

inline void AppendUniqueCoord(std::vector<glm::ivec3> &vec,
                              const glm::ivec3 &coord)
{
  for (const glm::ivec3 &c : vec)
  {
    if (c == coord)
    {
      return;
    }
  }
  vec.push_back(coord);
}

inline void ScheduleNeedRelightDirty(LitApplyPlan &plan,
                                     const ColumnChunkSnapshot &chunk,
                                     bool priority)
{
  if (priority)
  {
    AppendUniqueCoord(plan.mark_dirty_priority, chunk.coord);
  }
  else
  {
    AppendUniqueCoord(plan.mark_dirty, chunk.coord);
  }
  ++plan.schedule_n;
}

/// FD+drawable → PreferKick when pending GPU/RAA; Dirty on stall≥8 or
/// first NeedRelight entry (!is_dirty ∧ !pending). Avoid Dirty flood every MarkRelit.
/// A21-04 P2.3: ShouldForceDirtyAfterPreferKickStall requires pending — never
/// gate it under !pending (was a dead path). dirty∧pending∧no-progress∧stall≥limit
/// → PreferKick the pending job (not eternal note_prefer_kick_stall alone).
inline bool TryPreferKickOrForceDirty(LitApplyPlan &plan,
                                      const ColumnChunkSnapshot &chunk,
                                      bool pending_gpu_or_raa,
                                      bool force_stale_ticket)
{
  const bool fd_drawable = chunk.fully_dark && chunk.has_drawable;
  if (!fd_drawable)
  {
    return false;
  }
  if (ShouldPreferKickOverRemeshDirtyOnTicketedFullyDark(
          force_stale_ticket, chunk.fully_dark, chunk.has_drawable,
          pending_gpu_or_raa, chunk.has_publish_progress))
  {
    AppendUniqueCoord(plan.prefer_kick_gpu, chunk.coord);
    return true;
  }
  // Pending ownership: stall escape PreferKicks the live GPU/RAA job.
  if (chunk.is_dirty && pending_gpu_or_raa)
  {
    if (ShouldForceDirtyAfterPreferKickStall(
            fd_drawable, pending_gpu_or_raa, chunk.has_publish_progress,
            chunk.prefer_kick_stall_frames))
    {
      AppendUniqueCoord(plan.prefer_kick_gpu, chunk.coord);
      return true;
    }
    // Under stall_limit: wait for Kick; tick stall clock only.
    plan.note_prefer_kick_stall = true;
    return true;
  }
  // !pending: first NeedRelight entry (!is_dirty) → Dirty once.
  if (!pending_gpu_or_raa && !chunk.is_dirty)
  {
    ScheduleNeedRelightDirty(plan, chunk, /*priority=*/true);
    return true;
  }
  // Sysreset v5: dirty∧!pending∧stall≥8 → ForceDirty (not stall-only).
  if (ShouldForceDirtyWhenStuckDirtyNoPending(
          fd_drawable, chunk.is_dirty, pending_gpu_or_raa,
          chunk.prefer_kick_stall_frames))
  {
    ScheduleNeedRelightDirty(plan, chunk, /*priority=*/true);
    return true;
  }
  if (chunk.is_dirty)
  {
    plan.note_prefer_kick_stall = true;
    return true;
  }
  return false;
}

inline void ForceFirstMeshFromSkipDirty(LitApplyPlan &plan,
                                        const LitApplyColumnInput &in,
                                        const glm::ivec3 &coord)
{
  AppendUniqueCoord(plan.mark_dirty_priority, coord);
  ++plan.schedule_n;
  plan.enqueue_first_mesh = true;
  plan.first_mesh_column = in.column;
}

inline LitApplyPlan PlanPrimaryConsume(const LitApplyColumnInput &in)
{
  LitApplyPlan plan;
  plan.path = ColumnInstallPath::PrimaryConsume;
  plan.erase_pending_light = true;
  plan.erase_inflight = true;
  plan.fsm_after = ColumnEmergeState::Meshing;
  plan.persistence_light_complete = in.any_drawable;
  for (const ColumnChunkSnapshot &chunk : in.relit_chunks)
  {
    if (chunk.is_dirty || chunk.inflight || chunk.raa_pending)
    {
      if (!chunk.inflight &&
          ShouldBumpDirtyHeadForVisualHole(
              chunk.is_dirty, chunk.fully_dark, chunk.has_drawable,
              in.focus_horiz, in.consume_mode,
              ShouldRemeshAfterLitApplyForHole(chunk, in.force_stale_ticket)))
      {
        AppendUniqueCoord(plan.mark_dirty_priority, chunk.coord);
        ++plan.schedule_n;
      }
      else if (chunk.is_dirty &&
               ShouldForceFirstMeshOnSkipAlreadyDirty(
                   chunk.is_dirty, chunk.has_drawable, chunk.has_greedy,
                   chunk.soft_defer || in.soft_defer_empty_owned))
      {
        ForceFirstMeshFromSkipDirty(plan, in, chunk.coord);
      }
      else if (chunk.is_dirty)
      {
        ++plan.skip_already_dirty_n;
        // Ownership LightConverge: PreferKick only with progress∧pending;
        // else ForceDirtyStuck / stall (no PreferKick-spin without bake).
        (void)TryPreferKickOrForceDirty(plan, chunk,
                                        chunk.gpu_pending || chunk.raa_pending,
                                        in.force_stale_ticket);
      }
      else if (chunk.inflight)
      {
        ++plan.skip_inflight_n;
      }
      continue;
    }
    const bool needs_remesh =
        ShouldRemeshAfterLitApplyForHole(chunk, in.force_stale_ticket);
    if (!needs_remesh && chunk.has_drawable)
    {
      // A21 residual R2: equal-rev FullyDark used to `continue` here and never
      // PreferKick — prefer_kick_n stayed 0 while VB debt persisted. Legal cave
      // (settled, no dirty/ticket) may skip; repair-owned FD must stay live.
      if (chunk.fully_dark &&
          (chunk.is_dirty || in.has_repair_ticket || in.has_fm_ticket ||
           in.force_stale_ticket || !in.column_settled))
      {
        (void)TryPreferKickOrForceDirty(plan, chunk,
                                        chunk.gpu_pending || chunk.raa_pending,
                                        in.force_stale_ticket);
      }
      continue;
    }
    if (TryPreferKickOrForceDirty(plan, chunk,
                                  chunk.gpu_pending || chunk.raa_pending,
                                  in.force_stale_ticket))
    {
      if (chunk.raa_pending && !plan.prefer_kick_gpu.empty() &&
          plan.prefer_kick_gpu.back() == chunk.coord)
      {
        AppendUniqueCoord(plan.request_raa, chunk.coord);
      }
      continue;
    }
    // NeedRelight|NeedRemesh: schedule Dirty (co-publish light+mesh).
    (void)ColumnVisualStateForFullyDarkDrawable(chunk.fully_dark,
                                                chunk.has_drawable);
    if (needs_remesh)
    {
      AppendUniqueCoord(plan.mark_dirty_priority, chunk.coord);
      ++plan.schedule_n;
    }
    else
    {
      AppendUniqueCoord(plan.mark_dirty, chunk.coord);
      ++plan.schedule_n;
    }
    if (!chunk.has_drawable &&
        (chunk.has_greedy || chunk.soft_defer))
    {
      AppendUniqueCoord(plan.mark_dirty_priority, chunk.coord);
    }
  }
  return plan;
}

inline LitApplyPlan PlanPartialNoDirty(const LitApplyColumnInput &in)
{
  LitApplyPlan plan;
  plan.path = ColumnInstallPath::PartialNoDirty;
  plan.erase_inflight = true;
  plan.fsm_after = ColumnEmergeState::LitReady;
  (void)in;
  return plan;
}

inline LitApplyPlan PlanPrimaryStandard(const LitApplyColumnInput &in)
{
  LitApplyPlan plan;
  plan.path = ColumnInstallPath::PrimaryStandard;
  plan.erase_pending_light = true;
  plan.erase_inflight = true;
  plan.fsm_after = ColumnEmergeState::Meshing;
  plan.persistence_light_complete = in.any_drawable;
  if (in.damp_soft_empty_remesh)
  {
    for (const ColumnChunkSnapshot &chunk : in.relit_chunks)
    {
      if (!chunk.has_drawable && (chunk.has_greedy || chunk.soft_defer))
      {
        AppendUniqueCoord(plan.mark_dirty_priority, chunk.coord);
        plan.enqueue_first_mesh = true;
        plan.first_mesh_column = in.column;
      }
    }
    return plan;
  }
  for (const ColumnChunkSnapshot &chunk : in.relit_chunks)
  {
    if (chunk.is_dirty || chunk.inflight || chunk.raa_pending)
    {
      if (!chunk.inflight &&
          ShouldBumpDirtyHeadForVisualHole(
              chunk.is_dirty, chunk.fully_dark, chunk.has_drawable,
              in.focus_horiz, in.consume_mode,
              ShouldRemeshAfterLitApplyForHole(chunk, in.force_stale_ticket)))
      {
        AppendUniqueCoord(plan.mark_dirty_priority, chunk.coord);
        ++plan.schedule_n;
      }
      else if (chunk.is_dirty &&
               ShouldForceFirstMeshOnSkipAlreadyDirty(
                   chunk.is_dirty, chunk.has_drawable, chunk.has_greedy,
                   chunk.soft_defer || in.soft_defer_empty_owned))
      {
        ForceFirstMeshFromSkipDirty(plan, in, chunk.coord);
      }
      else if (chunk.is_dirty)
      {
        ++plan.skip_already_dirty_n;
        // Ownership LightConverge: PreferKick only with progress∧pending;
        // else ForceDirtyStuck / stall (no PreferKick-spin without bake).
        (void)TryPreferKickOrForceDirty(plan, chunk,
                                        chunk.gpu_pending || chunk.raa_pending,
                                        in.force_stale_ticket);
      }
      else if (chunk.inflight)
      {
        ++plan.skip_inflight_n;
      }
      continue;
    }
    RemeshAfterLitApplyDecision decision;
    const bool light_delta =
        chunk.still_stale ||
        (in.is_primary && chunk.fully_dark && !in.column_settled);
    if (!ShouldScheduleChunkRemesh(chunk, in.enter_quiesce, in.column_settled,
                                   in.sticky_owned, in.force_stale_ticket,
                                   light_delta, &decision))
    {
      if (decision == RemeshAfterLitApplyDecision::PreferKickGpu)
      {
        if (!TryPreferKickOrForceDirty(plan, chunk,
                                       chunk.gpu_pending || chunk.raa_pending,
                                       in.force_stale_ticket))
        {
          ScheduleNeedRelightDirty(plan, chunk, /*priority=*/true);
        }
      }
      else if (decision == RemeshAfterLitApplyDecision::SkipAlreadyDirty)
      {
        if (ShouldForceFirstMeshOnSkipAlreadyDirty(
                chunk.is_dirty, chunk.has_drawable, chunk.has_greedy,
                chunk.soft_defer || in.soft_defer_empty_owned))
        {
          ForceFirstMeshFromSkipDirty(plan, in, chunk.coord);
        }
        else
        {
          ++plan.skip_already_dirty_n;
        }
      }
      else if (decision == RemeshAfterLitApplyDecision::SkipInflight)
      {
        ++plan.skip_inflight_n;
      }
      else if (decision == RemeshAfterLitApplyDecision::SkipEnterLitQuiesce)
      {
        ++plan.suppress_enter_settled_n;
      }
      continue;
    }
    const bool needs_remesh =
        ShouldRemeshAfterLitApplyForHole(chunk, in.force_stale_ticket);
    if (!needs_remesh && chunk.has_drawable)
    {
      // A21 residual R2: equal-rev FullyDark used to `continue` here and never
      // PreferKick — prefer_kick_n stayed 0 while VB debt persisted. Legal cave
      // (settled, no dirty/ticket) may skip; repair-owned FD must stay live.
      if (chunk.fully_dark &&
          (chunk.is_dirty || in.has_repair_ticket || in.has_fm_ticket ||
           in.force_stale_ticket || !in.column_settled))
      {
        (void)TryPreferKickOrForceDirty(plan, chunk,
                                        chunk.gpu_pending || chunk.raa_pending,
                                        in.force_stale_ticket);
      }
      continue;
    }
    if (TryPreferKickOrForceDirty(plan, chunk,
                                  chunk.gpu_pending || chunk.raa_pending,
                                  in.force_stale_ticket))
    {
      if (chunk.raa_pending && !plan.prefer_kick_gpu.empty() &&
          plan.prefer_kick_gpu.back() == chunk.coord)
      {
        AppendUniqueCoord(plan.request_raa, chunk.coord);
      }
      continue;
    }
    // NeedRelight|NeedRemesh: schedule Dirty (co-publish light+mesh).
    (void)ColumnVisualStateForFullyDarkDrawable(chunk.fully_dark,
                                                chunk.has_drawable);
    if (in.priority_mesh && needs_remesh)
    {
      AppendUniqueCoord(plan.mark_dirty_priority, chunk.coord);
    }
    else
    {
      AppendUniqueCoord(plan.mark_dirty, chunk.coord);
    }
    ++plan.schedule_n;
  }
  if (in.had_mesh && !in.damp_soft_empty_remesh && !in.enter_quiesce &&
      ShouldEnqueueRemeshSeamAfterLit(in.had_mesh, in.enter_quiesce,
                                      in.any_drawable,
                                      /*column_remesh_owned=*/false))
  {
    plan.enqueue_first_mesh = true;
    plan.first_mesh_column = in.column;
  }
  return plan;
}

inline LitApplyPlan PlanColumnInstall(const LitApplyColumnInput &in)
{
  const ColumnInstallPath path = ClassifyColumnInstallPath(in);
  switch (path)
  {
  case ColumnInstallPath::PartialNoDirty:
    return PlanPartialNoDirty(in);
  case ColumnInstallPath::PrimaryConsume:
    return PlanPrimaryConsume(in);
  case ColumnInstallPath::PrimaryDefer:
  case ColumnInstallPath::PrimaryEnter:
  case ColumnInstallPath::PrimaryQuiesce:
  case ColumnInstallPath::PrimaryStandard:
    return PlanPrimaryStandard(in);
  case ColumnInstallPath::NeighborSeam:
  {
    LitApplyPlan plan;
    plan.path = ColumnInstallPath::NeighborSeam;
    plan.skip_neighbor = false;
    return plan;
  }
  default:
  {
    LitApplyPlan plan;
    plan.path = ColumnInstallPath::Skip;
    return plan;
  }
  }
}

} // namespace cutum
