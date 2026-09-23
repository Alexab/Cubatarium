#include "World/Streaming/ChunkRenderDemand.h"
#include "World/Streaming/RelightInstallPlanner.h"
#include "Render/Mesh/SeamCoverageManifest.h"

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
  store.NoteFaceDebtSatisfied(c, 0x01u, /*peer_gen=*/41);
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

  // A25/A31: orphan Created+no-progress is counted; A31 remints desire.
  {
    store.Clear();
    const glm::ivec3 orphan{9, 0, 9};
    Expect(store.NoteDemand(orphan, 1, 2) == DemandResult::NewDemand,
           "orphan NewDemand");
    ChunkRenderDemandRecord *orec = store.Find(orphan);
    Expect(orec && orec->has_active_attempt, "orphan has active");
    Expect(orec && orec->last_progress_ms <= 0.0, "orphan no progress");
    const auto r2 = store.ReconcileMaintenance(16);
    Expect(r2.orphan_active >= 1, "orphan counted");
    orec = store.Find(orphan);
    Expect(orec && orec->has_active_attempt, "A31 remints orphan desire");
    Expect(store.CancelOrphanActiveAttempts(16) >= 1, "orphan hard-cancelled");
    orec = store.Find(orphan);
    Expect(orec && !orec->has_active_attempt, "orphan cancelled");
  }

  // A25 R1: unload/reload — CancelledSuperseded then new desire.
  {
    store.Clear();
    const glm::ivec3 u{4, 1, 4};
    store.NoteDemand(u, 10, 11);
    store.NoteInstallResult(u, InstallResult::CancelledSuperseded);
    Expect(store.NoteDemand(u, 12, 13) == DemandResult::NewDemand,
           "reload NewDemand after cancel");
  }

  // A24 FREEZE helpers (never RemoveChunk pending FD; cooldown not flood).
  {
    using cutum::ShouldCooldownForceEqualRevPendingFullyDark;
    using cutum::ShouldDropPendingFullyDarkMesh;
    using cutum::ShouldHealFullyDarkWithRelightOnly;
    using cutum::ShouldHealFullyDarkWithRemesh;
    Expect(!ShouldDropPendingFullyDarkMesh(true, true, true),
           "A24 R1: never drop pending FD mesh");
    Expect(!ShouldCooldownForceEqualRevPendingFullyDark(
               true, true, false, 1, /*frames=*/10, /*cooldown=*/45),
           "A24 R3: cooldown not yet");
    Expect(ShouldCooldownForceEqualRevPendingFullyDark(
               true, true, false, 1, /*frames=*/45, /*cooldown=*/45),
           "A24 R3: cooldown fires at 45f");
    Expect(!ShouldCooldownForceEqualRevPendingFullyDark(
               true, true, /*any_light_rev_ahead=*/true, 1, 100, 45),
           "A24 R3: no force when light rev ahead");
    Expect(ShouldHealFullyDarkWithRemesh(true, false), "sky → remesh heal");
    Expect(ShouldHealFullyDarkWithRelightOnly(true, false, false),
           "void → relight-only");
  }

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

  // A26 N1: randomized event suite — reorder/cancel/unload/reload, 2 Y, 6 peers,
  // peer-ready-before-subscribe, stop→Published, zero orphan / infinite Retain.
  {
    using cutum::PeerReadyBeforeSubscribe;
    store.Clear();
    ChunkDemandCutoverEnabled() = true;
    const glm::ivec3 centers[2] = {{10, 0, 10}, {10, 1, 10}};
    const int dx[6] = {1, -1, 0, 0, 0, 0};
    const int dy[6] = {0, 0, 1, -1, 0, 0};
    const int dz[6] = {0, 0, 0, 0, 1, -1};
    unsigned seed = 0xA261u;
    auto rnd = [&seed]() -> unsigned {
      seed = seed * 1664525u + 1013904223u;
      return seed;
    };
    for (int iter = 0; iter < 48; ++iter)
    {
      const unsigned op = rnd() % 6u;
      const glm::ivec3 c0 = centers[rnd() % 2u];
      if (op == 0)
      {
        (void)store.NoteDemand(c0, 100 + (rnd() % 5u), 200 + (rnd() % 5u));
      }
      else if (op == 1)
      {
        store.NoteInstallResult(c0, InstallResult::CancelledSuperseded);
      }
      else if (op == 2)
      {
        store.NoteInstallResult(c0, InstallResult::RetainedAwaitingSuccessor);
        (void)store.NoteDemand(c0, 300 + (rnd() % 3u), 400 + (rnd() % 3u));
      }
      else if (op == 3)
      {
        const uint64_t g = 100 + (rnd() % 5u);
        const uint64_t l = 200 + (rnd() % 5u);
        store.NotePublishedRevs(c0, g, l);
        store.NoteInstallResult(c0, InstallResult::Published, g, l);
      }
      else if (op == 4)
      {
        const int f = static_cast<int>(rnd() % 6u);
        const glm::ivec3 peer{c0.x + dx[f], c0.y + dy[f], c0.z + dz[f]};
        const uint64_t need = 10 + (rnd() % 4u);
        store.NoteFaceDebt(c0, static_cast<uint8_t>(1u << f), need);
        const uint64_t pub = (rnd() & 1u) ? need : 0;
        Expect(PeerReadyBeforeSubscribe(pub, need) == (pub != 0),
               "peer-ready-before-subscribe");
        if (pub != 0)
        {
          store.NoteFaceDebtSatisfied(c0, static_cast<uint8_t>(1u << f), need);
        }
      }
      else
      {
        (void)store.ReconcileMaintenance(32);
      }
    }
    // Drain: publish every remaining desire, cancel orphans.
    for (const glm::ivec3 &c0 : centers)
    {
      for (int f = 0; f < 6; ++f)
      {
        const glm::ivec3 peer{c0.x + dx[f], c0.y + dy[f], c0.z + dz[f]};
        (void)peer;
      }
      if (ChunkRenderDemandRecord *r = store.Find(c0))
      {
        if (r->desired_geom_rev != 0 || r->desired_light_rev != 0)
        {
          store.NoteInstallResult(c0, InstallResult::Published,
                                  r->desired_geom_rev, r->desired_light_rev);
        }
      }
    }
    (void)store.ReconcileMaintenance(64);
    // A31: clear residual face debt + hard-cancel reminted Created orphans.
    for (const glm::ivec3 &c0 : centers)
    {
      store.NoteFaceDebtSatisfied(c0, /*face_mask=*/0x3Fu, /*peer_gen=*/100);
    }
    (void)store.CancelOrphanActiveAttempts(64);
    Expect(store.StopConverged(), "N1 stop converged after drain");
    Expect(store.CountUnsatisfiedDemands() == 0, "N1 zero unsatisfied");
  }

  // A26 N1: infinite Retain without successor desire fails StopConverged.
  {
    store.Clear();
    const glm::ivec3 bad{7, 0, 7};
    store.NoteDemand(bad, 1, 1);
    store.NoteInstallResult(bad, InstallResult::Published, 1, 1);
    store.NoteInstallResult(bad, InstallResult::RetainedAwaitingSuccessor);
    Expect(!store.StopConverged(), "infinite Retain fails stop");
    store.NoteDemand(bad, 2, 2);
    if (ChunkRenderDemandRecord *r = store.Find(bad))
    {
      // Progress past Created so StopConverged accepts live successor.
      store.NoteStageProgress(bad, cutum::JobStage::Admitted,
                              r->active_attempt_id);
    }
    Expect(store.StopConverged(), "Retain+successor desire ok");
  }

  if (gFails != 0)
  {
    std::fprintf(stderr, "chunk_render_demand_test failures=%d\n", gFails);
    return 1;
  }
  std::printf("chunk_render_demand_test OK\n");
  return 0;
}
