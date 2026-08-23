#pragma once

#include "World/Streaming/ColumnEmergeState.h"
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
      continue;
    }
    const bool needs_fm = !chunk.has_drawable || chunk.fully_dark;
    const bool stale = chunk.still_stale;
    const bool force = in.force_stale_ticket && chunk.fully_dark;
    if (needs_fm || stale || force)
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
      }
    }
    return plan;
  }
  for (const ColumnChunkSnapshot &chunk : in.relit_chunks)
  {
    if (chunk.is_dirty || chunk.inflight || chunk.raa_pending)
    {
      if (chunk.is_dirty)
      {
        ++plan.skip_already_dirty_n;
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
        AppendUniqueCoord(plan.prefer_kick_gpu, chunk.coord);
      }
      else if (decision == RemeshAfterLitApplyDecision::SkipAlreadyDirty)
      {
        ++plan.skip_already_dirty_n;
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
    const bool needs_fm = !chunk.has_drawable || chunk.fully_dark;
    if (in.priority_mesh && needs_fm)
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
