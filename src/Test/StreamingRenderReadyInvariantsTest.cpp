#include "World/Streaming/MeshLitGate.h"
#include "World/Streaming/MeshWorkAdmission.h"
#include "World/Streaming/ColumnRenderablePolicy.h"
#include "World/Streaming/ColumnFlowScheduler.h"

#include <cstdlib>
#include <iostream>
#include <vector>

using cutum::ShouldRejectDarkMeshCommit;
using cutum::SoftDeferMeshUntilLitPolicy;
using cutum::AllowUnlitFirstMesh;
using cutum::ClassifyStickyStaleDarkSoT;
using cutum::ColumnSoTKind;
using cutum::EnqueueStickyStaleRepairTickets;
using cutum::UColumnFlowScheduler;
using cutum::ColumnWorkKind;
using cutum::ComputeMeshWorkAdmission;
using cutum::FinalizeDrain;
using cutum::FinalizeSchedule;
using cutum::MeshWorkAdmission;
using cutum::MeshWorkAdmissionInput;

static int failures = 0;

static void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << std::endl;
    ++failures;
  }
}

int main()
{
  // SoftDefer: first-mesh in focus/underfeet never deferred (UnlitFirstMesh).
  // Remesh while pending stays deferred.
  // Contract: AdmitFocusVisibleMissing must MarkDirty while PendingLight
  // (manual 170154 forever-hole when Admit skipped Dirty).
  Expect(!SoftDeferMeshUntilLitPolicy(true, false, true, true, false, false),
         "underfeet missing+pending allows first mesh");
  Expect(SoftDeferMeshUntilLitPolicy(true, true, true, true, false, false),
         "underfeet has_mesh+pending defer remesh");
  Expect(!SoftDeferMeshUntilLitPolicy(true, true, false, true, false, false),
         "underfeet has_mesh+lit allow remesh");
  Expect(!SoftDeferMeshUntilLitPolicy(true, false, false, true, false, false),
         "underfeet missing+lit allow first mesh");

  // Focus missing + pending => allow first mesh (SoftDefer remesh-only).
  Expect(!SoftDeferMeshUntilLitPolicy(false, false, true, true, true, false),
         "focus missing+pending allows first mesh");
  // Focus missing + lit => allow.
  Expect(!SoftDeferMeshUntilLitPolicy(false, false, false, true, true, false),
         "focus missing+lit allow");

  // Remesh of existing while pending => defer (even with unlit allow).
  Expect(SoftDeferMeshUntilLitPolicy(false, true, true, true, true, true),
         "focus has_mesh+pending defer remesh despite unlit allow");
  Expect(!SoftDeferMeshUntilLitPolicy(false, true, false, true, true, false),
         "focus has_mesh+lit allow remesh");

  // Outside focus: pending always defers missing unless allow_unlit; else MayMesh.
  Expect(SoftDeferMeshUntilLitPolicy(false, false, true, false, true, false),
         "outside missing+pending must defer");
  Expect(!SoftDeferMeshUntilLitPolicy(false, false, true, false, true, true),
         "outside missing+pending+allow_unlit allows first mesh");
  Expect(SoftDeferMeshUntilLitPolicy(false, false, false, false, false, false),
         "outside missing !may_mesh defer");
  Expect(!SoftDeferMeshUntilLitPolicy(false, false, false, false, true, false),
         "outside missing may_mesh allow");

  Expect(!ShouldRejectDarkMeshCommit(false, true, true),
         "lit new mesh always commits");
  Expect(ShouldRejectDarkMeshCommit(true, true, false),
         "pending-light dark first mesh rejected when defer_until_lit");
  Expect(ShouldRejectDarkMeshCommit(true, false, true),
         "dark remesh must not replace lit mesh");
  Expect(!ShouldRejectDarkMeshCommit(true, false, false),
         "cave/unlit/empty-placeholder first mesh allowed when not deferred");
  // Empty SoftDefer placeholder: HasGreedy but !Drawable ⇒ had_lit_mesh=false
  // so place Immediate must commit (manual 184035 undrawn).
  Expect(!ShouldRejectDarkMeshCommit(true, false, /*had_lit_mesh=*/false),
         "empty SoftDefer placeholder Immediate dark commit allowed");

  // SoT AllowUnlitFirstMesh + SoftDefer allow_unlit_first_mesh.
  Expect(AllowUnlitFirstMesh(false, 2, false, true),
         "missing in focus → AllowUnlitFirstMesh");
  Expect(AllowUnlitFirstMesh(false, 5, true, true),
         "far missing in focus → AllowUnlitFirstMesh");
  Expect(!AllowUnlitFirstMesh(true, 1, true, true),
         "has_mesh never AllowUnlitFirstMesh");
  Expect(AllowUnlitFirstMesh(false, 5, false, true),
         "any FOV missing → AllowUnlitFirstMesh (land rim)");
  Expect(!AllowUnlitFirstMesh(false, 5, false, false),
         "outside focus → no AllowUnlitFirstMesh");
  Expect(!SoftDeferMeshUntilLitPolicy(false, false, true, true, true, true),
         "policy: pending+focus allows first mesh");
  Expect(!SoftDeferMeshUntilLitPolicy(false, false, true, true, true, false),
         "policy: focus missing never SoftDefer-skips first mesh");

  // TD-ARCH-026: SoT sticky/stale-dark (real invariants, not Expect(true)).
  {
    const auto no_mesh_sticky =
        ClassifyStickyStaleDarkSoT(/*has_mesh=*/false, /*sticky=*/true,
                                   /*stale=*/false, /*horiz=*/3);
    Expect(no_mesh_sticky.kind == ColumnSoTKind::StickyRemesh,
           "sticky without mesh → StickyRemesh");
    Expect(!no_mesh_sticky.draw_ok, "sticky without mesh → hide (no draw_ok)");
    Expect(no_mesh_sticky.has_repair_ticket,
           "sticky without mesh → has_repair_ticket");

    const auto meshed_sticky =
        ClassifyStickyStaleDarkSoT(true, true, false, 3);
    Expect(meshed_sticky.draw_ok && meshed_sticky.has_repair_ticket,
           "meshed sticky → draw_ok + repair ticket");

    const auto stale =
        ClassifyStickyStaleDarkSoT(true, false, true, 3);
    Expect(stale.kind == ColumnSoTKind::StaleDark, "stale-dark kind");
    Expect(stale.draw_ok && stale.has_repair_ticket,
           "meshed stale-dark → draw_ok + repair ticket");

    const auto near = ClassifyStickyStaleDarkSoT(false, true, false, 1);
    Expect(near.kind == ColumnSoTKind::None,
           "near ring uses other path (SoT sticky classifier idle)");
  }

  // Hide/sticky/stale without mesh ⇒ scheduler contains RemeshSeam|RelightThenMesh.
  {
    UColumnFlowScheduler sched;
    const glm::ivec2 focus{10, 20};
    std::vector<glm::ivec2> sticky{{12, 20}};      // horiz=2 → near Relight
    std::vector<glm::ivec2> stale{{15, 20}};       // horiz=5 → far Remesh+Relight
    EnqueueStickyStaleRepairTickets(sched, focus, sticky, stale);
    Expect(sched.Contains(sticky[0], ColumnWorkKind::RemeshSeam),
           "sticky → RemeshSeam ticket");
    Expect(sched.Contains(sticky[0], ColumnWorkKind::RelightThenMesh),
           "near sticky → RelightThenMesh");
    Expect(sched.Contains(stale[0], ColumnWorkKind::RemeshSeam),
           "stale-dark → RemeshSeam");
    Expect(sched.Contains(stale[0], ColumnWorkKind::RelightThenMesh),
           "stale-dark → RelightThenMesh");
    Expect(sched.Contains(sticky[0], ColumnWorkKind::PromoteRelight) ||
               sched.Contains(sticky[0], ColumnWorkKind::RemeshSeam),
           "sticky near → live repair ticket kinds");
  }

  // MeshWorkAdmission: floors propose, Finalize caps under backlog.
  {
    MeshWorkAdmissionInput normal{};
    normal.pending_gpu = 4;
    const auto a0 = ComputeMeshWorkAdmission(normal);
    Expect(a0.mode == MeshWorkAdmission::Mode::Normal, "pending<12 → Normal");
    Expect(FinalizeSchedule(16, a0) == 16, "Normal Finalize passthrough schedule");

    MeshWorkAdmissionInput warm{};
    warm.pending_gpu = 14;
    warm.visual_holes = false;
    const auto a1 = ComputeMeshWorkAdmission(warm);
    Expect(a1.mode == MeshWorkAdmission::Mode::WarmBacklog, "pending>=12 !holes → Warm");
    Expect(FinalizeSchedule(16, a1) == 6, "Warm caps schedule at 6");
    Expect(a1.gpu_apply_max >= 16, "Warm GPU boost");

    MeshWorkAdmissionInput hole{};
    hole.pending_gpu = 14;
    hole.pending_gpu_queued = 10;
    hole.visual_holes = true;
    hole.moving = true;
    const auto a2 = ComputeMeshWorkAdmission(hole);
    Expect(a2.mode == MeshWorkAdmission::Mode::HoleDrain, "pending+holes → HoleDrain");
    Expect(FinalizeSchedule(16, a2) == 5, "HoleDrain schedule covers FM4+remesh1");
    Expect(!a2.allow_neighbor_dirty, "HoleDrain denies neighbor Dirty");
    Expect(a2.admit_batch == 1, "HoleDrain admit_batch=1 while moving");
    Expect(a2.gpu_apply_max >= 16, "HoleDrain GPU boost under miss");
    Expect(FinalizeDrain(4, a2) >= 12, "HoleDrain drain floor");
    Expect(a2.softdefer_requeue >= 2, "HoleDrain Held requeue ≥2 under holes (G3)");

    MeshWorkAdmissionInput hole_idle = hole;
    hole_idle.moving = false;
    const auto a2i = ComputeMeshWorkAdmission(hole_idle);
    Expect(a2i.admit_batch >= 2, "HoleDrain idle admit_batch>=2");
    Expect(FinalizeSchedule(16, a2i) >= 4, "HoleDrain idle schedule floor ≥4");
    Expect(a2i.first_mesh_schedule >= 4, "HoleDrain idle first_mesh headroom");

    MeshWorkAdmissionInput warm_hole{};
    warm_hole.pending_gpu = 18;
    warm_hole.visual_holes = true;
    warm_hole.moving = true;
    const auto a3 = ComputeMeshWorkAdmission(warm_hole);
    Expect(a3.mode == MeshWorkAdmission::Mode::HoleDrain, "pending>=16+holes still HoleDrain");
    Expect(FinalizeSchedule(16, a3) <= 5, "warm holes schedule capped");
    Expect(a3.first_mesh_schedule >= 4, "HoleDrain first_mesh floor while moving");
    Expect(FinalizeSchedule(16, a3) >= a3.first_mesh_schedule,
           "schedule covers first_mesh quota");

    MeshWorkAdmissionInput deep{};
    deep.pending_gpu = 30;
    deep.visual_holes = true;
    const auto a4 = ComputeMeshWorkAdmission(deep);
    Expect(a4.mode == MeshWorkAdmission::Mode::DeepBacklog, "pending>=24 → Deep");
    Expect(FinalizeSchedule(16, a4) <= 5, "Deep schedule capped");
    Expect(a4.softdefer_requeue >= 1, "Deep Held requeue ≥1 under holes (G3)");
    Expect(a4.first_mesh_schedule >= 4, "Deep first_mesh under holes");
    Expect(FinalizeSchedule(16, a4) >= a4.first_mesh_schedule,
           "Deep schedule covers first_mesh");

    // F0 hysteresis: stay in HoleDrain until holes clear AND pending≤8.
    MeshWorkAdmissionInput sticky_exit = hole;
    sticky_exit.pending_gpu = 10;
    sticky_exit.visual_holes = true;
    sticky_exit.prev_mode =
        static_cast<uint8_t>(MeshWorkAdmission::Mode::HoleDrain);
    const auto a5 = ComputeMeshWorkAdmission(sticky_exit);
    Expect(a5.mode == MeshWorkAdmission::Mode::HoleDrain,
           "hysteresis keeps HoleDrain while holes");

    MeshWorkAdmissionInput cool_holes = sticky_exit;
    cool_holes.pending_gpu = 6;
    cool_holes.visual_holes = true;
    const auto a6 = ComputeMeshWorkAdmission(cool_holes);
    Expect(a6.mode == MeshWorkAdmission::Mode::HoleDrain,
           "hysteresis keeps HoleDrain at pending<=8 with holes");

    MeshWorkAdmissionInput cool_clear = sticky_exit;
    cool_clear.pending_gpu = 6;
    cool_clear.visual_holes = false;
    cool_clear.missing_underfeet = false;
    // Explicit Queued telem (else MeshWorkQueuedApprox treats pending as queued).
    cool_clear.pending_gpu_queued = 0;
    cool_clear.pending_gpu_kicked = 6;
    const auto a7 = ComputeMeshWorkAdmission(cool_clear);
    Expect(a7.mode == MeshWorkAdmission::Mode::Normal,
           "hysteresis exits when !holes && pending<=8");

    MeshWorkAdmissionInput warm_hold = sticky_exit;
    warm_hold.pending_gpu = 10;
    warm_hold.visual_holes = false;
    warm_hold.prev_mode =
        static_cast<uint8_t>(MeshWorkAdmission::Mode::HoleDrain);
    const auto a8 = ComputeMeshWorkAdmission(warm_hold);
    Expect(a8.mode == MeshWorkAdmission::Mode::WarmBacklog,
           "hysteresis Warm while pending>8 without holes");

    // F1: enqueue_gpu_budget tracks ring − kicked.
    MeshWorkAdmissionInput ring{};
    ring.pending_gpu = 14;
    ring.pending_gpu_kicked = 3;
    ring.visual_holes = true;
    ring.moving = true;
    ring.ring_depth = 8;
    const auto a9 = ComputeMeshWorkAdmission(ring);
    Expect(a9.mode == MeshWorkAdmission::Mode::HoleDrain, "ring HoleDrain");
    Expect(a9.enqueue_gpu_budget == 5, "HoleDrain enqueue = ring-kicked");
    Expect(a9.gpu_apply_max >= 16, "HoleDrain apply_max ≥ ring*2");
    Expect(a9.first_mesh_schedule >= 4, "HoleDrain first_mesh≥4 moving (H)");
    Expect(a9.remesh_schedule <= 1, "HoleDrain remesh≤1");
    Expect(a9.max_schedule >= a9.first_mesh_schedule + a9.remesh_schedule,
           "max_schedule covers split quotas");

    // H: light_debt must not crush FirstMesh below HoleDrain quota.
    MeshWorkAdmissionInput debt = ring;
    debt.unfinished_visual = 12;
    debt.pending_gpu = 14;
    debt.visual_holes = true;
    debt.moving = true;
    const auto a9d = ComputeMeshWorkAdmission(debt);
    Expect(a9d.mode == MeshWorkAdmission::Mode::HoleDrain, "debt HoleDrain");
    Expect(a9d.first_mesh_schedule >= 4, "light_debt keeps first_mesh≥4");
    Expect(a9d.starve_remesh_horiz >= 2, "light_debt remesh keep_h≥2 for stale");
    Expect(FinalizeSchedule(16, a9d) >= a9d.first_mesh_schedule,
           "light_debt schedule covers first_mesh");

    // G0: holes + queued ≥ ring → HoleDrain even when pending cooled below 12.
    MeshWorkAdmissionInput refill{};
    refill.pending_gpu = 10;
    refill.pending_gpu_queued = 8;
    refill.visual_holes = true;
    refill.moving = true;
    refill.ring_depth = 8;
    const auto a10 = ComputeMeshWorkAdmission(refill);
    Expect(a10.mode == MeshWorkAdmission::Mode::HoleDrain,
           "G0 holes+queued≥ring → HoleDrain despite pending<12");
    Expect(FinalizeSchedule(20, a10) <= 5, "G0 latch caps FOV floor sch=20");

    MeshWorkAdmissionInput warm_pend{};
    warm_pend.pending_gpu = 10;
    warm_pend.pending_gpu_queued = 3;
    warm_pend.visual_holes = true;
    warm_pend.moving = true;
    warm_pend.ring_depth = 8;
    const auto a10b = ComputeMeshWorkAdmission(warm_pend);
    Expect(a10b.mode == MeshWorkAdmission::Mode::HoleDrain,
           "G0 holes+pending≥8 → HoleDrain even if queued<ring/2");
    Expect(FinalizeSchedule(12, a10b) <= 5, "G0 warm-pending caps sch=12");

    MeshWorkAdmissionInput refill_exit{};
    refill_exit.pending_gpu = 6;
    refill_exit.pending_gpu_queued = 8;
    refill_exit.visual_holes = false;
    refill_exit.ring_depth = 8;
    refill_exit.prev_mode =
        static_cast<uint8_t>(MeshWorkAdmission::Mode::HoleDrain);
    const auto a11 = ComputeMeshWorkAdmission(refill_exit);
    Expect(a11.mode == MeshWorkAdmission::Mode::WarmBacklog,
           "G0 hysteresis: queued>ring/2 blocks Normal exit");

    MeshWorkAdmissionInput refill_clear = refill_exit;
    refill_clear.pending_gpu_queued = 2;
    const auto a12 = ComputeMeshWorkAdmission(refill_clear);
    Expect(a12.mode == MeshWorkAdmission::Mode::Normal,
           "G0 exits when !holes && pending≤8 && queued≤ring/2");

    // I: unfinished_visual≥8 counts as holes for G0 latch (160240 thrash).
    MeshWorkAdmissionInput uv_holes{};
    uv_holes.pending_gpu = 10;
    uv_holes.pending_gpu_queued = 3;
    uv_holes.visual_holes = false;
    uv_holes.missing_underfeet = false;
    uv_holes.unfinished_visual = 14;
    uv_holes.moving = true;
    uv_holes.ring_depth = 8;
    const auto a13 = ComputeMeshWorkAdmission(uv_holes);
    Expect(a13.mode == MeshWorkAdmission::Mode::HoleDrain,
           "I UV≥8 + pending≥8 → HoleDrain without visual_holes");
    Expect(FinalizeSchedule(12, a13) <= 5, "I UV holes caps FOV sch=12");

    // Queued refill without visual holes → Warm (not Normal/sch=12).
    MeshWorkAdmissionInput q_warm{};
    q_warm.pending_gpu = 10;
    q_warm.pending_gpu_queued = 8;
    q_warm.visual_holes = false;
    q_warm.unfinished_visual = 0;
    q_warm.ring_depth = 8;
    const auto a14 = ComputeMeshWorkAdmission(q_warm);
    Expect(a14.mode == MeshWorkAdmission::Mode::WarmBacklog,
           "queued>half-ring + pending≥8 → Warm without holes");
    Expect(FinalizeSchedule(12, a14) <= 6, "Warm caps FOV sch=12");

    // J0: holes + cooled pending still HoleDrain (no FOV Normal refill).
    MeshWorkAdmissionInput cool_holes_j0{};
    cool_holes_j0.pending_gpu = 1;
    cool_holes_j0.pending_gpu_queued = 0;
    cool_holes_j0.pending_gpu_kicked = 1;
    cool_holes_j0.visual_holes = false;
    cool_holes_j0.unfinished_visual = 9;
    cool_holes_j0.moving = true;
    cool_holes_j0.ring_depth = 8;
    const auto a15 = ComputeMeshWorkAdmission(cool_holes_j0);
    Expect(a15.mode == MeshWorkAdmission::Mode::HoleDrain,
           "J0 UV holes + pending=1 → HoleDrain not Normal");
    Expect(FinalizeSchedule(12, a15) <= 5, "J0 cool holes caps sch=12");

    // J1: miss backlog HoleDrain prefers Finish wall budget.
    MeshWorkAdmissionInput finish_bias{};
    finish_bias.pending_gpu = 16;
    finish_bias.pending_gpu_queued = 8;
    finish_bias.pending_gpu_kicked = 8;
    finish_bias.visual_holes = true;
    finish_bias.moving = true;
    finish_bias.ring_depth = 8;
    const auto a16 = ComputeMeshWorkAdmission(finish_bias);
    Expect(a16.mode == MeshWorkAdmission::Mode::HoleDrain, "J1 backlog HoleDrain");
    Expect(a16.gpu_budget_frac >= 0.82, "J1 Finish budget frac under miss backlog");
  }

  if (failures != 0)
  {
    std::cerr << failures << " failure(s)" << std::endl;
    return 1;
  }
  std::cout << "streaming_render_ready_invariants_test: PASS" << std::endl;
  return 0;
}
