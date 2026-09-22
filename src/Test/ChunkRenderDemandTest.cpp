#include "World/Streaming/ChunkRenderDemand.h"
#include "World/Streaming/RelightInstallPlanner.h"

#include <cstdio>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    ++gFails;
  }
}

} // namespace

int main()
{
  using cutum::ChunkDemandAllowsColumnFaceDebtClear;
  using cutum::ChunkDemandCutoverEnabled;
  using cutum::ChunkRenderDemandRecord;
  using cutum::ColumnChunkSnapshot;
  using cutum::DemandResult;
  using cutum::InstallResult;
  using cutum::LitApplyPlan;
  using cutum::TryPreferKickOrForceDirty;
  using cutum::UChunkRenderDemandStore;

  UChunkRenderDemandStore &store = UChunkRenderDemandStore::Get();
  store.Clear();

  const glm::ivec3 c{1, 2, 3};
  Expect(store.NoteDemand(c, 5, 7) == DemandResult::NewDemand, "first NewDemand");
  Expect(store.NoteDemand(c, 5, 7) == DemandResult::Coalesced, "same Coalesced");

  ChunkRenderDemandRecord *rec = store.Find(c);
  Expect(rec != nullptr, "record exists");
  if (rec)
  {
    rec->published_geom_rev = 5;
    rec->published_light_rev = 7;
    rec->retained_awaiting_successor = false;
  }
  Expect(store.NoteDemand(c, 5, 7) == DemandResult::AlreadySatisfied,
         "published meets desired");

  store.NoteInstallResult(c, InstallResult::RetainedAwaitingSuccessor);
  Expect(store.NoteDemand(c, 8, 9) == DemandResult::NewDemand,
         "retain keeps successor NewDemand");
  rec = store.Find(c);
  Expect(rec && rec->retained_awaiting_successor, "retain flag set");
  Expect(rec && rec->desired_light_rev == 9, "successor light desire");

  store.NoteFaceDebt(c, 0x03u, /*peer_gen=*/42);
  store.NoteFaceDebtSatisfied(c, 0x01u, /*peer_gen=*/99);
  Expect(rec && (rec->face_debt_mask & 0x01u) != 0,
         "stale peer_gen keeps face0 debt");
  store.NoteFaceDebtSatisfied(c, 0x01u, /*peer_gen=*/42);
  Expect(rec && (rec->face_debt_mask & 0x01u) == 0, "matching peer clears face0");
  Expect(rec && (rec->face_debt_mask & 0x02u) != 0, "face1 kept");

  // Cutover: column FaceDebt clear forbidden while flag ON (default ON).
  ChunkDemandCutoverEnabled() = true;
  Expect(!ChunkDemandAllowsColumnFaceDebtClear(), "cutover blocks column clear");
  ChunkDemandCutoverEnabled() = false;
  Expect(ChunkDemandAllowsColumnFaceDebtClear(), "rollback restores column clear");
  ChunkDemandCutoverEnabled() = true; // restore default for process

  const auto recon = store.ReconcileMaintenance(8);
  Expect(recon.checked >= 1, "reconcile scanned");

  // A21-04: dirty+pending+no-progress+stall≥limit → PreferKick, not stall-only.
  ColumnChunkSnapshot chunk;
  chunk.coord = c;
  chunk.fully_dark = true;
  chunk.has_drawable = true;
  chunk.is_dirty = true;
  chunk.gpu_pending = true;
  chunk.has_publish_progress = false;
  chunk.prefer_kick_stall_frames = 100;
  LitApplyPlan plan;
  Expect(TryPreferKickOrForceDirty(plan, chunk, true, true), "planner acted");
  Expect(!plan.prefer_kick_gpu.empty(), "PreferKick pending job");
  Expect(plan.mark_dirty.empty() && plan.mark_dirty_priority.empty(),
         "no ForceDirty on pending path");
  const bool stall_only =
      plan.prefer_kick_gpu.empty() && plan.note_prefer_kick_stall;
  Expect(!stall_only, "not stall-only at age100");

  // Residual R2: equal-rev FullyDark with repair ticket must PreferKick/Dirty,
  // not silent continue (PlanPrimaryConsume early-out regression).
  {
    using cutum::LitApplyColumnInput;
    using cutum::PlanPrimaryConsume;
    LitApplyColumnInput in{};
    in.column = {1, 3};
    in.is_primary = true;
    in.consume_mode = true;
    in.has_repair_ticket = true;
    in.column_settled = false;
    in.focus_horiz = 2;
    ColumnChunkSnapshot fd{};
    fd.coord = c;
    fd.fully_dark = true;
    fd.has_drawable = true;
    fd.is_dirty = true;
    fd.gpu_pending = false;
    fd.meshed_light_rev = 7;
    fd.light_field_rev = 7; // equal-rev → NeedsRemesh false
    fd.prefer_kick_stall_frames = 100;
    in.relit_chunks.push_back(fd);
    LitApplyPlan p2 = PlanPrimaryConsume(in);
    Expect(p2.note_prefer_kick_stall || !p2.prefer_kick_gpu.empty() ||
               !p2.mark_dirty.empty() || !p2.mark_dirty_priority.empty(),
           "equal-rev FD with repair stays live");
  }

  // Under stall_limit: stall note only.
  LitApplyPlan plan_early;
  chunk.prefer_kick_stall_frames = 3;
  Expect(TryPreferKickOrForceDirty(plan_early, chunk, true, true),
         "early stall acted");
  Expect(plan_early.prefer_kick_gpu.empty(), "no PreferKick under limit");
  Expect(plan_early.note_prefer_kick_stall, "note stall under limit");

  if (gFails != 0)
  {
    std::fprintf(stderr, "chunk_render_demand_test failures=%d\n", gFails);
    return 1;
  }
  std::printf("chunk_render_demand_test OK\n");
  return 0;
}
