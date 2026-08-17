#include "World/Streaming/MeshWorkAdmission.h"
#include "World/Streaming/SoftDeferEmptyPolicy.h"
#include "World/Streaming/SoftDeferFramePolicy.h"
#include "World/Streaming/AntiFlickerPolicy.h"
#include "World/Streaming/VisualStagePolicy.h"
#include "World/Streaming/CyOrderPolicy.h"
#include "World/Streaming/EnterVisualGate.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Streaming/ColumnVisualReadyPolicy.h"
#include "World/Diagnostics/EnterLitDiagnostics.h"
#include "World/Streaming/NearFovWorkPriority.h"
#include "World/Streaming/OceanCruisePolicy.h"
#include "World/Streaming/RelightFifoPolicy.h"
#include "World/Streaming/FrontierStagePolicy.h"
#include "World/Streaming/OceanFrontierPolicy.h"

#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <vector>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

} // namespace

int main()
{
  using cutum::ComputeMeshWorkAdmission;
  using cutum::IsMissFirstMeshClass;
  using cutum::IsNearFocusMissUrgent;
  using cutum::IsSoftDeferEmptyPlaceholder;
  using cutum::MeshWorkAdmission;
  using cutum::MeshWorkAdmissionInput;
  using cutum::ShouldColdAsyncImmEscape;
  using cutum::ShouldEnqueueSoftDeferEmptyFirstMesh;

  Expect(IsMissFirstMeshClass(true, 0, 5), "cy0 tops class");
  Expect(IsMissFirstMeshClass(true, 3, 5), "Era20: cy3 tops class");
  Expect(IsMissFirstMeshClass(true, 5, 4), "Era20: mh4 tops class");
  Expect(!IsMissFirstMeshClass(true, 5, 5), "cy5 mh5 outside class");
  Expect(!IsMissFirstMeshClass(false, 0, 0), "no holes → no class");

  Expect(IsNearFocusMissUrgent(true, false, 2), "nh2 near urgent");
  Expect(!IsNearFocusMissUrgent(true, false, 4), "nh4 rim not urgent");
  Expect(IsNearFocusMissUrgent(true, true, 5), "underfeet urgent");
  Expect(!IsNearFocusMissUrgent(false, false, 0), "no miss not urgent");

  {
    MeshWorkAdmissionInput near{};
    near.visual_holes = true;
    near.nearest_miss_horiz = 1;
    near.nearest_miss_cy = 0;
    near.pending_gpu = 28;
    const MeshWorkAdmission a = ComputeMeshWorkAdmission(near);
    Expect(a.mode == MeshWorkAdmission::Mode::HoleDrain,
           "near miss pending=28 stays HoleDrain not DeepBacklog");
    Expect(a.max_schedule >= 8, "near miss keeps max_schedule≥8");
    near.prev_mode =
        static_cast<uint8_t>(MeshWorkAdmission::Mode::DeepBacklog);
    const MeshWorkAdmission b = ComputeMeshWorkAdmission(near);
    Expect(b.mode == MeshWorkAdmission::Mode::HoleDrain,
           "hysteresis does not force DeepBacklog on near miss");
  }

  {
    MeshWorkAdmissionInput rim{};
    rim.visual_holes = true;
    rim.nearest_miss_horiz = 5;
    rim.nearest_miss_cy = 5;
    rim.unfinished_visual = 12;
    rim.pending_gpu = 16;
    const MeshWorkAdmission a = ComputeMeshWorkAdmission(rim);
    Expect(a.mode == MeshWorkAdmission::Mode::HoleDrain,
           "rim visual_holes stays HoleDrain for K3 band");
    Expect(a.mode != MeshWorkAdmission::Mode::DeepBacklog,
           "rim-only pending=16 not DeepBacklog");
  }

  Expect(ShouldColdAsyncImmEscape(true, 0), "miss+async0 escape");
  Expect(ShouldColdAsyncImmEscape(true, 1), "miss+async1 escape");
  Expect(!ShouldColdAsyncImmEscape(true, 2), "async>=2 no escape");
  Expect(!ShouldColdAsyncImmEscape(false, 0), "no miss no escape");

  Expect(IsSoftDeferEmptyPlaceholder(true, false, false, false, false, true),
         "empty SoftDefer placeholder");
  Expect(!IsSoftDeferEmptyPlaceholder(true, true, false, false, false, true),
         "drawable not empty");
  Expect(!ShouldEnqueueSoftDeferEmptyFirstMesh(true, 2, false),
         "no FM without miss or in_focus");
  Expect(ShouldEnqueueSoftDeferEmptyFirstMesh(true, 2, false, true),
         "Era32 P3: empty in_focus → FM");
  Expect(ShouldEnqueueSoftDeferEmptyFirstMesh(true, 0, true),
         "empty while miss → FM");
  Expect(!ShouldEnqueueSoftDeferEmptyFirstMesh(false, 5, true),
         "not placeholder → no FM");

  // --- Era22 SoftDefer Heal SLA / VB ticket predicates ---
  using cutum::AsyncScheduleFloorUnderMiss;
  using cutum::ShouldEnqueueNearestVbNoTicket;
  using cutum::ShouldMissTimeSlaKick;
  using cutum::ShouldScheduleFirstMeshUnderSoftDefer;
  using cutum::SoftDeferHeldCountsAsRepairProgress;
  using cutum::VisibleBlackTicketCollectRadius;

  Expect(ShouldScheduleFirstMeshUnderSoftDefer(false, true),
         "Era22 I-S1: !Drawable + miss/focus → schedule FirstMesh");
  Expect(!ShouldScheduleFirstMeshUnderSoftDefer(true, true),
         "Era22 I-S1: drawable remesh stays SoftDefer-drop");
  Expect(!ShouldScheduleFirstMeshUnderSoftDefer(false, false),
         "Era22 I-S1: outside miss/focus → Held path, not force schedule");
  Expect(SoftDeferHeldCountsAsRepairProgress(true),
         "Era22 I-S2: SoftDeferHeld ∈ repair progress");
  Expect(!SoftDeferHeldCountsAsRepairProgress(false),
         "Era22 I-S2: no Held → no progress credit");
  Expect(VisibleBlackTicketCollectRadius(5, true, true) == 5,
         "Era22 I-V3: no_ticket → full focus collect");
  Expect(VisibleBlackTicketCollectRadius(5, true, false) == 2,
         "Era22 I-V3: miss without no_ticket may keep r≤2");
  Expect(VisibleBlackTicketCollectRadius(5, false, false) == 5,
         "Era22 I-V3: !miss → full focus");
  Expect(ShouldEnqueueNearestVbNoTicket(true, true),
         "Era22 I-V3: no_ticket + async_ok → enqueue");
  Expect(!ShouldEnqueueNearestVbNoTicket(true, false),
         "Era22 I-V3: async saturated → skip nearest enqueue storm");
  Expect(!ShouldEnqueueNearestVbNoTicket(false, true),
         "Era22 I-V3: no orphan → no dedicated nearest enqueue");
  Expect(ShouldMissTimeSlaKick(true, 2),
         "Era22 I-M8: miss age ≥2 periods → PreferKick");
  Expect(!ShouldMissTimeSlaKick(true, 1),
         "Era22 I-M8: age < SLA → no PreferKick storm");
  Expect(!ShouldMissTimeSlaKick(false, 10),
         "Era22 I-M8: no miss → no time SLA");
  Expect(AsyncScheduleFloorUnderMiss(true) == 0,
         "Closeout F: AsyncScheduleFloor folded into pools");
  Expect(AsyncScheduleFloorUnderMiss(false) == 0,
         "Closeout F: AsyncScheduleFloor off when calm");

  // --- Era23 Void Relight / rim miss / place-hole predicates ---
  using cutum::ShouldForceFirstMeshOnPlaceHole;
  using cutum::ShouldNotePendingLightOnVoidEnqueue;
  using cutum::ShouldPreferKickMissWitnessEarly;
  using cutum::ShouldPreferKickSoftDeferEmptyStuck;
  using cutum::ShouldReserveVoidRelightSlots;
  using cutum::SoftDeferHeldCountsAsVoidProgress;
  using cutum::VoidRelightCollectCap;

  Expect(!SoftDeferHeldCountsAsVoidProgress(true, true),
         "Era23 I-V6: Held + fully-dark ⇒ not void progress");
  Expect(SoftDeferHeldCountsAsVoidProgress(true, false),
         "Era23 I-V6: Held without fully-dark still counts");
  Expect(!SoftDeferHeldCountsAsVoidProgress(false, true),
         "Era23 I-V6: no Held → no progress");
  Expect(ShouldReserveVoidRelightSlots(250, 0, true),
         "Era23 I-V4: void_n>T ⇒ Relight slots");
  Expect(ShouldReserveVoidRelightSlots(250, 0, false),
         "Era23 I-V4: void_n>T even without miss");
  Expect(ShouldReserveVoidRelightSlots(40, 2, true),
         "Era23 I-V4: miss + void faces ⇒ Relight slots");
  // Era31 I-T1 / Era32 P1: VB reserves Relight (not RemeshSeam-as-heal).
  Expect(ShouldReserveVoidRelightSlots(0, 3, true),
         "Era31/32: miss + VB ⇒ Relight reserve");
  Expect(ShouldReserveVoidRelightSlots(40, 2, false),
         "Era31/32: idle VB ⇒ Relight reserve");
  Expect(ShouldReserveVoidRelightSlots(0, 3, false),
         "Era31/32: idle VB ⇒ Relight reserve");
  Expect(!ShouldReserveVoidRelightSlots(50, 0, false),
         "Era23 I-V4: calm void below T → no reserve");
  Expect(VoidRelightCollectCap(2, true) >= 2,
         "Era23 I-V4: void pressure keeps void_cap≥2");
  Expect(VoidRelightCollectCap(1, true) >= 2,
         "Era23 I-V4: void pressure floors cap at 2");
  Expect(ShouldNotePendingLightOnVoidEnqueue(true),
         "Era23 I-V5: fully-dark/no_sky ⇒ Note on enqueue");
  Expect(!ShouldNotePendingLightOnVoidEnqueue(false),
         "Era23 I-V5: lit remesh path → no void Note");
  Expect(ShouldPreferKickMissWitnessEarly(true, true),
         "Era23 I-M9: miss + FirstMesh class → PreferKick every frame");
  Expect(!ShouldPreferKickMissWitnessEarly(true, false),
         "Era23 I-M9: miss outside class → age SLA only");
  Expect(!ShouldPreferKickMissWitnessEarly(false, true),
         "Era23 I-M9: no miss → no early PreferKick");

  Expect(ShouldPreferKickSoftDeferEmptyStuck(true, true, true),
         "Era23 P2: SoftDefer empty + Queued/Kicked ⇒ PreferKick");
  Expect(!ShouldPreferKickSoftDeferEmptyStuck(true, true, false),
         "Era23 P2: empty without GPU stuck → no PreferKick");
  Expect(!ShouldPreferKickSoftDeferEmptyStuck(true, false, true),
         "Era23 P2: no miss → no SoftDefer PreferKick");

  Expect(ShouldForceFirstMeshOnPlaceHole(true, true),
         "Era23 I-P1: empty/undrawn near ⇒ FirstMesh");
  Expect(!ShouldForceFirstMeshOnPlaceHole(true, false),
         "Era23 I-P1: far empty → no force");
  Expect(!ShouldForceFirstMeshOnPlaceHole(false, true),
         "Era23 I-P1: drawable near → no force");

  // --- Era24 SoftDefer empty FirstMesh-until-Drawable ---
  using cutum::ShouldEscalateSoftDeferEmptyAge;
  using cutum::SoftDeferEmptyHealKind;
  using cutum::SoftDeferEmptyHealKindOf;
  using cutum::SoftDeferEmptyNeedsFirstMeshOwnership;

  Expect(SoftDeferEmptyNeedsFirstMeshOwnership(true, true),
         "Era24 I-E2: empty + miss/focus ⇒ FirstMesh ownership");
  Expect(!SoftDeferEmptyNeedsFirstMeshOwnership(true, false),
         "Era24 I-E2: empty outside FOV → no ownership");
  Expect(!SoftDeferEmptyNeedsFirstMeshOwnership(false, true),
         "Era24 I-E2: not empty → no ownership");
  Expect(!ShouldEscalateSoftDeferEmptyAge(14),
         "Era33: age 14 < sla 15 → no escalate");
  Expect(ShouldEscalateSoftDeferEmptyAge(15),
         "Era33: age 15 ≥ sla → escalate");
  Expect(ShouldEscalateSoftDeferEmptyAge(60, 45),
         "Era32: explicit sla 45 still honored");
  Expect(ShouldEscalateSoftDeferEmptyAge(44),
         "Era33: default sla 15 → age 44 escalates");
  Expect(SoftDeferEmptyHealKindOf() == SoftDeferEmptyHealKind::FirstMesh,
         "Era24 I-E3: SoftDefer empty heal is FirstMesh only");
  Expect(ShouldPreferKickSoftDeferEmptyStuck(true, true, true),
         "Era24 KEEP: PreferKick SoftDefer empty only Queued/Kicked");
  Expect(!ShouldPreferKickSoftDeferEmptyStuck(true, true, false),
         "Era24 KEEP: idle SoftDefer empty → no PreferKick storm");

  // --- Era25 Frontier Column Stage SLA ---
  using cutum::FrontierColumnNeedsFirstMeshAfterLit;
  using cutum::FrontierColumnNeedsLightTicket;
  using cutum::FrontierNearLoadOpsFloor;
  using cutum::IsFrontierPressure;

  Expect(IsFrontierPressure(1, 0, true, 0),
         "Era25 I-F4: gen_backlog + miss ⇒ frontier pressure");
  Expect(IsFrontierPressure(0, 2, false, 250),
         "Era25 I-F4: async_queued + void>T ⇒ frontier pressure");
  Expect(IsFrontierPressure(0, 0, true, 999),
         "Era30 I-O1: void>T without gen/async ⇒ ocean heal pressure");
  Expect(IsFrontierPressure(0, 0, false, 50, 200, 3),
         "Era30 I-O1: VB without gen/async ⇒ ocean heal pressure");
  Expect(!IsFrontierPressure(3, 0, false, 50),
         "Era25 I-F4: gen without miss/void → no pressure");
  Expect(IsFrontierPressure(0, 0, false, 0, 200, 0, 5),
         "sky-fix: moving absent columns ⇒ frontier pressure");
  Expect(!IsFrontierPressure(0, 0, false, 0, 200, 0, 0),
         "sky-fix: no absent/miss/void → no empty-gen pressure");
  Expect(FrontierColumnNeedsLightTicket(true, true, false, false),
         "Era25 I-F2: near + pending + !drawable ⇒ light ticket");
  Expect(FrontierColumnNeedsLightTicket(true, true, true, true),
         "Era25 I-F2: near + pending + fully-dark ⇒ light ticket");
  Expect(!FrontierColumnNeedsLightTicket(true, true, true, false),
         "Era25 I-F2: drawable lit → no light ticket");
  Expect(!FrontierColumnNeedsLightTicket(false, true, false, true),
         "Era25 I-F2: far → no light ticket");
  Expect(FrontierColumnNeedsFirstMeshAfterLit(true, true, false, true),
         "Era25 I-F3: near lit solid !drawable ⇒ FirstMesh");
  Expect(!FrontierColumnNeedsFirstMeshAfterLit(true, true, true, true),
         "Era25 I-F3: drawable → no FirstMesh");
  Expect(!FrontierColumnNeedsFirstMeshAfterLit(true, false, false, true),
         "Era25 I-F3: not lit → no FirstMesh yet");
  Expect(FrontierNearLoadOpsFloor(true, true, 2) == 3,
         "Era25 I-F5: frontier moving floor ops ≥3");
  Expect(FrontierNearLoadOpsFloor(true, true, 5) == 5,
         "Era25 I-F5: keep higher base ops");
  Expect(FrontierNearLoadOpsFloor(false, true, 2) == 2,
         "Era25 I-F5: no pressure → base ops");
  using cutum::FrontierNearLoadRadius;
  Expect(FrontierNearLoadRadius(true, true, 2, 4) == 4,
         "Era25 I-F5: frontier NearLoad radius ≥ focus");
  Expect(FrontierNearLoadRadius(false, true, 2, 4) == 2,
         "Era25 I-F5: !frontier keeps NearLoad radius clamp");

  // --- Era26 Ocean Dual-Debt ---
  using cutum::CollectFullyDarkSkipsOnlyRelightOwnership;
  using cutum::ShouldDrainPendingLightUnderMissMoving;
  using cutum::ShouldPreserveVoidBgSlotsUnderRimSla;
  using cutum::SoftDeferEmptyNeedsParallelVoidRelight;
  using cutum::VoidRelightCollectRadius;

  Expect(ShouldDrainPendingLightUnderMissMoving(true, true, 250, 0),
         "Era26 I-O1: miss+moving+void>T ⇒ drain");
  Expect(ShouldDrainPendingLightUnderMissMoving(true, true, 0, 5),
         "Era26 I-O1: miss+moving+VB ⇒ drain");
  Expect(!ShouldDrainPendingLightUnderMissMoving(true, true, 10, 0),
         "Era26 I-O1: miss+moving void≤T no VB → no drain");
  Expect(!ShouldDrainPendingLightUnderMissMoving(false, true, 500, 10),
         "Era26 I-O1: !miss → no special drain");
  Expect(!ShouldDrainPendingLightUnderMissMoving(true, false, 500, 10),
         "Era26 I-O1: !moving → idle path owns drain");
  Expect(VoidRelightCollectRadius(4, true, true, false) == 4,
         "Era26 I-O1: void_pressure ⇒ full focus radius");
  Expect(VoidRelightCollectRadius(4, true, false, false) == 2,
         "Era26 I-O1: miss !void_pressure ⇒ clamp 2");
  Expect(VoidRelightCollectRadius(4, false, false, false) == 4,
         "Era26 I-O1: !miss ⇒ full radius");
  Expect(CollectFullyDarkSkipsOnlyRelightOwnership(true),
         "Era26 I-O3: Relight/Pending ⇒ skip Collect");
  Expect(!CollectFullyDarkSkipsOnlyRelightOwnership(false),
         "Era26 I-O3: FirstMesh-only/Dirty → no skip");
  Expect(ShouldPreserveVoidBgSlotsUnderRimSla(true, true),
         "Era26 I-O2: rim+void_slots ⇒ preserve bg");
  Expect(!ShouldPreserveVoidBgSlotsUnderRimSla(true, false),
         "Era26 I-O2: rim without void → normal clamp");
  Expect(SoftDeferEmptyNeedsParallelVoidRelight(true, true),
         "Era26 I-O4: empty+void ⇒ parallel Relight");
  Expect(!SoftDeferEmptyNeedsParallelVoidRelight(true, false),
         "Era26 I-O4: empty lit → no parallel Relight");
  Expect(!SoftDeferEmptyNeedsParallelVoidRelight(false, true),
         "Era26 I-O4: not empty → no parallel Relight");
  {
    int bmin = 40;
    int bmax = 50;
    cutum::FillWaterLateralRemeshBand(true, 3, 64, 256, bmin, bmax, 16);
    Expect(bmin <= 48 && bmax >= 80,
           "Era26 I-O5: FillWater lateral widens sea±CHUNK");
    int keep_min = 40;
    int keep_max = 50;
    cutum::FillWaterLateralRemeshBand(true, 1, 64, 256, keep_min, keep_max, 16);
    Expect(keep_min == 40 && keep_max == 50,
           "Era26 I-O5: horiz≤1 leaves band to underfeet path");
  }

  // --- Era27 Anti-Flicker Ownership ---
  using cutum::ShouldDampMarkRelitRemeshOnSoftDeferEmpty;
  using cutum::ShouldHoldInflightSupersedeUnderMiss;
  using cutum::ShouldRetargetSoftDeferCaptureWitness;
  using cutum::SoftDeferEmptyAgeShouldReset;
  using cutum::kSoftDeferCaptureWitnessPinFrames;

  Expect(kSoftDeferCaptureWitnessPinFrames == 8, "Era27 I-A1: pin_T default 8");
  Expect(ShouldRetargetSoftDeferCaptureWitness(false, 0, 8, false, true),
         "Era27 I-A1: !pin_valid ⇒ retarget");
  Expect(!ShouldRetargetSoftDeferCaptureWitness(true, 3, 8, false, true),
         "Era27 I-A1: pin live + still empty ⇒ hold");
  Expect(ShouldRetargetSoftDeferCaptureWitness(true, 8, 8, false, true),
         "Era27 I-A1: pin_age ≥ T ⇒ retarget");
  Expect(ShouldRetargetSoftDeferCaptureWitness(true, 2, 8, true, true),
         "Era27 I-A1: better horiz ⇒ retarget");
  Expect(ShouldRetargetSoftDeferCaptureWitness(true, 2, 8, false, false),
         "Era27 I-A1: pinned healed ⇒ retarget");
  Expect(!SoftDeferEmptyAgeShouldReset(true, false),
         "Era27 I-A2: still empty no progress ⇒ sticky age");
  Expect(SoftDeferEmptyAgeShouldReset(false, false),
         "Era27 I-A2: healed ⇒ reset age");
  Expect(SoftDeferEmptyAgeShouldReset(true, true),
         "Era27 I-A2: progress ⇒ reset age");
  Expect(ShouldDampMarkRelitRemeshOnSoftDeferEmpty(true, false),
         "Era27 I-A3: SoftDefer-empty owned !Drawable ⇒ damp remesh");
  Expect(!ShouldDampMarkRelitRemeshOnSoftDeferEmpty(true, true),
         "Era27 I-A3: Drawable lit⇒relit remesh KEEP");
  Expect(!ShouldDampMarkRelitRemeshOnSoftDeferEmpty(false, false),
         "Era27 I-A3: not SoftDefer-owned → no damp");
  Expect(ShouldHoldInflightSupersedeUnderMiss(true, true, false),
         "Era27 I-A4: miss+Inflight !Drawable ⇒ hold supersede");
  Expect(!ShouldHoldInflightSupersedeUnderMiss(true, true, true),
         "Era27 I-A4: Drawable → normal supersede OK");
  Expect(!ShouldHoldInflightSupersedeUnderMiss(false, true, false),
         "Era27 I-A4: !miss → no hold");

  // --- Era39 SoftDefer anti-flicker + hidden-neighbor seam ---
  {
    using cutum::ShouldRemeshDrawableForHiddenNeighborSeam;
    using cutum::SoftDeferEmptyShouldApplyOwnership;
    using cutum::SoftDeferEmptyShouldKeepOwnership;
    using cutum::SoftDeferEmptyShouldMarkDirtyAfterAvoid;
    using cutum::IsSoftDeferHiddenNeighbor;

    Expect(SoftDeferEmptyShouldKeepOwnership(true, true),
           "Era39: sticky own while still empty");
    Expect(!SoftDeferEmptyShouldKeepOwnership(false, true),
           "Era39: drop own when healed");
    Expect(!SoftDeferEmptyShouldKeepOwnership(true, false),
           "Era39: no sticky without prior ownership");
    Expect(!SoftDeferEmptyShouldMarkDirtyAfterAvoid(true, 0),
           "Era39: damp Dirty while FM/pending after avoid");
    Expect(!SoftDeferEmptyShouldMarkDirtyAfterAvoid(false, 2),
           "Era39: damp Dirty until min_frames");
    Expect(SoftDeferEmptyShouldMarkDirtyAfterAvoid(false, 4),
           "Era39: Dirty OK after min_frames without ticket");
    Expect(SoftDeferEmptyShouldApplyOwnership(true),
           "Era39: apply ownership when Cd ready");
    Expect(!SoftDeferEmptyShouldApplyOwnership(false),
           "Era39: count-only while Cd cooling");
    Expect(ShouldRemeshDrawableForHiddenNeighborSeam(true, true, false),
           "Era39: remesh drawable on SoftDefer-hidden enter");
    Expect(ShouldRemeshDrawableForHiddenNeighborSeam(true, false, true),
           "Era39: remesh drawable on SoftDefer-hidden leave/heal");
    Expect(!ShouldRemeshDrawableForHiddenNeighborSeam(true, true, true),
           "Era39: no remesh when hidden state unchanged");
    Expect(!ShouldRemeshDrawableForHiddenNeighborSeam(false, true, false),
           "Era39: skip remesh if self !Drawable");
    Expect(IsSoftDeferHiddenNeighbor(true, false, true),
           "Era39: loaded SoftDefer empty/held = hidden neighbor");
    Expect(!IsSoftDeferHiddenNeighbor(true, true, true),
           "Era39: drawable neighbor not hidden");
    Expect(!IsSoftDeferHiddenNeighbor(false, false, true),
           "Era39: unloaded → Unknown path, not SoftDefer-hidden");
  }

  // --- Era28 Visual Stage Gate ---
  using cutum::ShouldAllowUnlitFirstMeshNearFov;
  using cutum::ShouldPublishMeshToDraw;
  using cutum::ShouldRelightBeforeDrawNear;
  using cutum::ShouldRemeshAfterApplyOnlyWhileBuilding;
  using cutum::SoftDeferEmptyShouldMarkDirty;
  using cutum::kVisualStageNearFovHoriz;
  using cutum::kVisualStageLitDrawableHoriz;
  using cutum::ShouldHideFullyDarkUntilLitInRing;

  Expect(kVisualStageNearFovHoriz == 2, "Era28 I-V1: near_r default 2");
  Expect(kVisualStageLitDrawableHoriz == 4, "Era32 I-L1: lit ring default 4");
  Expect(!ShouldAllowUnlitFirstMeshNearFov(false, true, 1, 2),
         "Era28 I-V1: near horiz⇒ no Unlit");
  Expect(!ShouldAllowUnlitFirstMeshNearFov(false, true, 2, 2),
         "Era28 I-V1: horiz==near_r ⇒ no Unlit");
  Expect(ShouldAllowUnlitFirstMeshNearFov(false, true, 3, 2),
         "Era28 I-V1: far of near_r=2 ⇒ Unlit OK");
  Expect(!ShouldAllowUnlitFirstMeshNearFov(
             false, true, 3, kVisualStageLitDrawableHoriz),
         "Era32: mid lit-ring ⇒ no Unlit");
  Expect(ShouldAllowUnlitFirstMeshNearFov(
             false, true, 5, kVisualStageLitDrawableHoriz),
         "Era32: hinterland ⇒ Unlit OK");
  Expect(!ShouldAllowUnlitFirstMeshNearFov(true, true, 5, 2),
         "Era28 I-V1: has_mesh ⇒ no Unlit");
  Expect(ShouldHideFullyDarkUntilLitInRing(3, true, false),
         "Era32: fully-dark mid-ring hide without ocean_heal");
  Expect(ShouldHideFullyDarkUntilLitInRing(4, true, false),
         "Era32: fully-dark at ring edge hide");
  Expect(!ShouldHideFullyDarkUntilLitInRing(5, true, false),
         "Era32: hinterland fully-dark no ring hide");
  Expect(ShouldHideFullyDarkUntilLitInRing(2, true, true),
         "Era32: fully-dark never keep-prior (hole > black plug)");
  Expect(!SoftDeferEmptyShouldMarkDirty(true, true, false),
         "Era28 I-V2: FM ticket ⇒ no Dirty");
  Expect(!SoftDeferEmptyShouldMarkDirty(true, false, true),
         "Era28 I-V2: Inflight/Pending ⇒ no Dirty");
  Expect(SoftDeferEmptyShouldMarkDirty(true, false, false),
         "Era28 I-V2: dead ownership ⇒ MarkDirty");
  Expect(!SoftDeferEmptyShouldMarkDirty(false, false, false),
         "Era28 I-V2: not empty ⇒ no Dirty");
  Expect(ShouldPublishMeshToDraw(true, false, true),
         "Era28 I-V1: lit ⇒ publish");
  Expect(ShouldPublishMeshToDraw(false, true, true),
         "Era28 I-V1: keep-prior ⇒ publish");
  Expect(!ShouldPublishMeshToDraw(false, false, true),
         "Era28 I-V1: Unlit preview alone ⇒ no publish");
  Expect(ShouldRelightBeforeDrawNear(true, true, false),
         "Era28 I-V4: near+void !lit ⇒ Relight before draw");
  Expect(!ShouldRelightBeforeDrawNear(true, true, true),
         "Era28 I-V4: already lit ⇒ no Relight-before-draw");
  Expect(ShouldRemeshAfterApplyOnlyWhileBuilding(true, false),
         "Era28 I-V3: Building !lit ⇒ RemeshAfterApply only");
  Expect(!ShouldRemeshAfterApplyOnlyWhileBuilding(true, true),
         "Era28 I-V3: lit drawable ⇒ normal remesh OK");
  using cutum::SoftDeferEmptyPreferKickAfterAgeOnly;
  Expect(SoftDeferEmptyPreferKickAfterAgeOnly(true, true, true, true),
         "Era28 P2: age SLA + Queued ⇒ PreferKick");
  Expect(!SoftDeferEmptyPreferKickAfterAgeOnly(false, true, true, true),
         "Era28 P2: before age SLA ⇒ no PreferKick");
  Expect(!SoftDeferEmptyPreferKickAfterAgeOnly(true, true, true, false),
         "Era28 P2: age SLA without GPU stuck ⇒ no PreferKick");

  // --- Era29 Enter Visual Warmup ---
  using cutum::EnterOpaqueChurnSoftMax;
  using cutum::EnterSoftDeferEmptyNeedsFirstMesh;
  using cutum::EnterSpawnCapturePinFrames;
  using cutum::EnterUnderfeetNeedsLitDrawable;
  using cutum::EnterVisualWarmupAppUpdateSoftMs;
  using cutum::EnterVisualWarmupRadiusChunks;
  using cutum::CollectFullyDarkShouldSkipForOwnership;
  using cutum::ShouldDampFarUnlitRemeshOnLit;
  using cutum::ShouldRemeshAfterApplyOnlyOnIdleDrawable;
  using cutum::ShouldRunEnterStreamingWarmupDespiteSpawnPrepared;

  Expect(EnterVisualWarmupRadiusChunks() == kVisualStageLitDrawableHoriz,
         "Era33 P0: enter visual radius = LitDrawable ring 4");
  Expect(EnterUnderfeetNeedsLitDrawable(false, false),
         "Era29 I-E1: !lit !keep-prior ⇒ needs warmup");
  Expect(!EnterUnderfeetNeedsLitDrawable(true, false),
         "Era29 I-E1: lit drawable ⇒ ready");
  Expect(!EnterUnderfeetNeedsLitDrawable(false, true),
         "Era29 I-E1: keep-prior ⇒ ready");
  Expect(EnterSoftDeferEmptyNeedsFirstMesh(true, true),
         "Era29 I-E4: empty underfeet ⇒ FirstMesh");
  Expect(!EnterSoftDeferEmptyNeedsFirstMesh(true, false),
         "Era29 I-E4: empty far → no enter FirstMesh force");
  Expect(ShouldRunEnterStreamingWarmupDespiteSpawnPrepared(true),
         "Era29 I-E2: always warmup despite coop prepare");
  Expect(ShouldRunEnterStreamingWarmupDespiteSpawnPrepared(false),
         "Era29 I-E2: warmup when !prepared");
  Expect(EnterVisualWarmupAppUpdateSoftMs() == 200,
         "Era29 I-E5: enter_app soft ≤200");
  Expect(EnterOpaqueChurnSoftMax() == 200,
         "Era29 P4: opaque churn soft max 200");
  Expect(EnterSpawnCapturePinFrames() == 16,
         "Era29 P3: enter Capture pin 16 frames");
  Expect(!CollectFullyDarkShouldSkipForOwnership(0, true),
         "Era29 P3: near PendingLight ⇒ no CollectFullyDark skip");
  Expect(CollectFullyDarkShouldSkipForOwnership(3, true),
         "Era29 P3: far PendingLight ⇒ skip CollectFullyDark");
  Expect(ShouldDampFarUnlitRemeshOnLit(true, 3),
         "Era29 P3: far drawable Unlit ⇒ damp remesh-on-lit");
  Expect(!ShouldDampFarUnlitRemeshOnLit(true, 1),
         "Era29 P3: near drawable ⇒ no far damp");
  Expect(ShouldRemeshAfterApplyOnlyOnIdleDrawable(true, true),
         "Era29 P4: idle drawable ⇒ RemeshAfterApply");
  Expect(!ShouldRemeshAfterApplyOnlyOnIdleDrawable(true, false),
         "Era29 P4: idle !drawable ⇒ no RemeshAfterApply-only");

  // --- Era30 Ocean Cruise SLA ---
  using cutum::EnterVisualWarmupHardCapMs;
  using cutum::FluidMapShouldThrottleCruise;
  using cutum::IsOceanHealPressure;
  using cutum::OceanCaptureWitnessPinFrames;
  using cutum::OceanVoidRelightDrainCapMoving;
  using cutum::ShouldDampOceanCaptureRetarget;
  using cutum::ShouldDrainPendingLightUnderOceanVoid;
  using cutum::ShouldFrontierPressureDespiteEmptyGen;
  using cutum::ShouldSkipStaleRemeshForPendingVoid;

  Expect(IsOceanHealPressure(false, 250, 0), "Era30 I-O1: void>T ⇒ heal pressure");
  Expect(IsOceanHealPressure(false, 0, 2), "Era30 I-O1: VB ⇒ heal pressure");
  Expect(!IsOceanHealPressure(false, 50, 0), "Era30 I-O1: calm void/VB");
  Expect(ShouldFrontierPressureDespiteEmptyGen(true, true, true),
         "Era30 I-O1: empty gen+async + ocean heal");
  Expect(!ShouldFrontierPressureDespiteEmptyGen(true, true, false),
         "Era30 I-O1: empty queues without heal");
  Expect(OceanVoidRelightDrainCapMoving(true, 1) == 2,
         "Era30 I-O2: void moving drain cap floor");
  Expect(!ShouldSkipStaleRemeshForPendingVoid(true, true),
         "Era30 I-O3: void column keeps Relight path");
  Expect(ShouldSkipStaleRemeshForPendingVoid(true, false),
         "Era30 I-O3: stale-only pending skips remesh");
  Expect(FluidMapShouldThrottleCruise(40, 35.0, true, 250, 0),
         "Era30 I-O4: cruise throttle under void heal");
  Expect(!FluidMapShouldThrottleCruise(40, 35.0, false, 250, 0),
         "Era30 I-O4: idle → no cruise throttle");
  Expect(EnterVisualWarmupHardCapMs() == 120000,
         "Era42: enter lit warn wall default 120s");
  Expect(OceanCaptureWitnessPinFrames() == 12,
         "Era30 I-O5: ocean Capture pin 12 frames");
  Expect(!ShouldDampOceanCaptureRetarget(true, 3, true),
         "Era30 I-O5: damp horiz≥2 retarget on ocean heal");
  Expect(ShouldDrainPendingLightUnderOceanVoid(true, 250, 0),
         "Era30 I-O3: moving void drain without miss");

  // --- Era31 Ocean Heal Throughput ---
  using cutum::OceanHealMeshEmergeBudgetMs;
  using cutum::OceanHealMovingRelightDrainFloor;
  using cutum::OceanHealRelightCarveOutMs;
  using cutum::OceanHealVoidRelightNoteMinPerFrame;
  using cutum::ShouldCountVisibleBlackProgress;
  using cutum::ShouldForceEnterVisualCap;
  using cutum::ShouldHideDrawableUntilLitNearRim;
  using cutum::ShouldRemeshAfterApplyOnlyOnMovingCruiseHeal;

  Expect(OceanHealVoidRelightNoteMinPerFrame() == 2,
         "Era31/32: void Note min 2/frame");
  Expect(OceanHealMovingRelightDrainFloor(true, true, 250) == 2,
         "Era31/32: moving void drain floor 2");
  Expect(OceanHealMovingRelightDrainFloor(true, true, 50) == 1,
         "Era31/32: moving VB-only drain floor 1");
  Expect(OceanHealMeshEmergeBudgetMs() <= 16.0,
         "Era31 I-T2: emerge cap ≤16ms");
  Expect(OceanHealRelightCarveOutMs() >= 4.0,
         "Era31 I-T2: Relight carve-out ≥4ms");
  Expect(!ShouldCountVisibleBlackProgress(true, true, false),
         "Era31 I-T3: fully-dark ⇒ no VB progress");
  Expect(ShouldCountVisibleBlackProgress(true, true, true),
         "Era31 I-T3: pending lit slot ⇒ progress OK");
  Expect(ShouldCountVisibleBlackProgress(true, false, false),
         "Era31 I-T3: not fully-dark ⇒ progress OK");
  Expect(ShouldHideDrawableUntilLitNearRim(true, 1, true, false),
         "Era32: near rim hide until lit (universal ring)");
  Expect(ShouldHideDrawableUntilLitNearRim(false, 1, true, false),
         "Era32: hide without ocean_heal");
  Expect(ShouldHideDrawableUntilLitNearRim(true, 4, true, false),
         "Era32: lit-ring edge hide");
  Expect(!ShouldHideDrawableUntilLitNearRim(true, 5, true, false),
         "Era32: hinterland no ring hide");
  Expect(ShouldRemeshAfterApplyOnlyOnMovingCruiseHeal(true, true, true),
         "Era31 I-T5: moving cruise heal ⇒ RemeshAfterApply-only");
  Expect(!ShouldForceEnterVisualCap(250.0, false),
         "Era41: 250ms without ready ⇒ no force (was load 200ms abort)");
  Expect(ShouldForceEnterVisualCap(50.0, true),
         "Era41: mesh soft ready ⇒ force cap");
  Expect(!ShouldForceEnterVisualCap(50.0, false),
         "Era41: early GpuWarmup without ready ⇒ no cap");
  Expect(!ShouldForceEnterVisualCap(250.0, false, /*cold_create=*/true),
         "Era41: cold create also waits FOV (same hard-wall)");
  Expect(!ShouldForceEnterVisualCap(120000.0, false),
         "Era42: require_zero ⇒ hard-wall does not force with debt");
  Expect(!ShouldForceEnterVisualCap(120000.0, false, /*cold_create=*/true),
         "Era42: require_zero holds cold create past warn wall");
  Expect(ShouldForceEnterVisualCap(120000.0, false, /*cold_create=*/false,
                                   /*hard_wall_ms=*/-1,
                                   /*require_zero=*/false),
         "Era42: require_zero=false ⇒ hard-wall abort for debug");
  Expect(ShouldForceEnterVisualCap(250.0, true, /*cold_create=*/true),
         "Era41: soft-ready ⇒ force regardless of create/load");

  // --- Era41/Era42 Enter lit budgets / progress ---
  {
    using cutum::EnterFovLitHardWallMs;
    using cutum::EnterFovLitProgressFraction;
    using cutum::EnterFovRelightApplyBudget;
    using cutum::EnterFovRelightCaptureBudget;
    using cutum::ShouldHoldEnterBarForFovLit;
    Expect(EnterFovLitHardWallMs() == 120000, "Era42: lit warn wall 120s");
    Expect(EnterFovRelightCaptureBudget() >= 16, "Era42: Capture budget ≥16");
    Expect(EnterFovRelightApplyBudget() >= 64, "Era42: apply budget ≥64");
    Expect(EnterFovLitProgressFraction(0, 10) == 1.0f,
           "Era41: zero debt ⇒ progress 1");
    Expect(EnterFovLitProgressFraction(10, 10) == 0.0f,
           "Era41: full debt ⇒ progress 0");
    Expect(EnterFovLitProgressFraction(2, 10) >
               EnterFovLitProgressFraction(8, 10),
           "Era41: debt↓ ⇒ progress↑");
    Expect(ShouldHoldEnterBarForFovLit(5, 1000.0),
           "Era41: hold bar while FOV debt");
    Expect(!ShouldHoldEnterBarForFovLit(0, 1000.0),
           "Era41: no hold when debt cleared");
    Expect(ShouldHoldEnterBarForFovLit(5, 120000.0),
           "Era42: require_zero holds past warn wall");
    Expect(!ShouldHoldEnterBarForFovLit(5, 120000.0, EnterFovLitHardWallMs(),
                                       /*require_zero=*/false),
           "Era42: require_zero=false releases past wall");
  }

  // --- Era43 Enter lit gate / snapshot ---
  {
    using cutum::ShouldBlockNotePendingOutsideSnapshot;
    using cutum::ShouldSkipEnterStreamingWarmup;
    Expect(ShouldSkipEnterStreamingWarmup(true),
           "Era43: skip streaming when enter lit gate active");
    Expect(!ShouldSkipEnterStreamingWarmup(false),
           "Era43: streaming OK when gate inactive");
    Expect(ShouldBlockNotePendingOutsideSnapshot(true, false),
           "Era43: block NotePending outside snapshot");
    Expect(!ShouldBlockNotePendingOutsideSnapshot(true, true),
           "Era43: allow NotePending inside snapshot");
    const std::vector<int> unresolved{0, 2};
    int debt = 0;
    for (int i = 0; i < 3; ++i)
    {
      if (std::find(unresolved.begin(), unresolved.end(), i) !=
          unresolved.end())
      {
        ++debt;
      }
    }
    Expect(debt == 2, "Era43: snapshot debt counts unresolved cols");
  }

  // --- Era43f/Era44 Enter mesh warmup drain / abort ---
  {
    using cutum::EnterGpuWarmupMonotonicProgress;
    using cutum::EnterGpuWarmupProgressFraction;
    using cutum::IsEnterGpuWarmupReady;
    using cutum::ShouldContinueEnterMeshWarmupDrain;
    using cutum::ShouldForceEnterInGameAfterAbortDrain;
    using cutum::ShouldForceEnterMeshAbort;
    Expect(!ShouldContinueEnterMeshWarmupDrain(false, false, 0),
           "Era43f: no drain work when all clear");
    Expect(ShouldContinueEnterMeshWarmupDrain(false, false, 3),
           "Era43f: drain when gpu_pending remains");
    Expect(ShouldContinueEnterMeshWarmupDrain(true, false, 0),
           "Era43f: drain when spawn meshes pending");
    Expect(ShouldForceEnterMeshAbort(0, false, 120000.0, 120000),
           "Era44: mesh abort when lit done and ring not ready");
    Expect(!ShouldForceEnterMeshAbort(5, false, 120000.0, 120000),
           "Era44: no mesh abort while lit debt remains");
    Expect(!ShouldForceEnterMeshAbort(0, true, 120000.0, 120000),
           "Era44: no mesh abort when ring ready");
    Expect(EnterGpuWarmupProgressFraction(0, 10, 0, 10, 0, 10, 0, 10) == 1.0f,
           "Era44: zero debt ⇒ progress 1");
    Expect(EnterGpuWarmupProgressFraction(10, 10, 10, 10, 10, 10, 10, 10) ==
               0.0f,
           "Era44: full debt ⇒ progress 0");
    float display = 0.0f;
    Expect(EnterGpuWarmupMonotonicProgress(0.2f, display) == 0.2f,
           "Era44: monotonic progress increases");
    Expect(EnterGpuWarmupMonotonicProgress(0.1f, display) == 0.2f,
           "Era44: monotonic progress never regresses");
    Expect(!IsEnterGpuWarmupReady(false, 0, true, true),
           "Era44: not ready without ring");
    Expect(IsEnterGpuWarmupReady(true, 0, true, true),
           "Era44: ready when ring+mesh+lit+min frames");
    Expect(!ShouldForceEnterInGameAfterAbortDrain(100000.0, 300000),
           "Era44: force_ingame not before wall");
    Expect(ShouldForceEnterInGameAfterAbortDrain(300000.0, 300000),
           "Era44: force_ingame at wall");
  }

  // --- Era51 mesh warmup progress + cruise stabilize ---
  {
    using cutum::EnterLitSample;
    using cutum::FormatMeshWarmupProgress;
    using cutum::IsEnterGpuWarmupReady;
    using cutum::MeshWarmupResolvedFraction;
    using cutum::NeedsCruiseStabilize;
    Expect(FormatMeshWarmupProgress(465, 100) ==
               std::string("Building meshes... 365/465 (100 pending)"),
           "Era51: progress uses queue depth");
    Expect(MeshWarmupResolvedFraction(465, 100) > 0.78f,
           "Era51: resolved fraction from pending");
    EnterLitSample sample{};
    Expect(!NeedsCruiseStabilize(sample, 0, false, 0),
           "Era51: cruise ready when clean");
    sample.mesh_dirty = true;
    Expect(NeedsCruiseStabilize(sample, 0, false, 0),
           "Era51: cruise needs mesh drain");
    Expect(IsEnterGpuWarmupReady(true, 0, true, true, true),
           "SOTA: enter ready is ring+lit+mesh+vis (no cruise extra gate)");
    using cutum::ShouldResetRenderStateForGpuWarmup;
    using cutum::ShouldWarmupGreedyGpuDuringEnter;
    Expect(!ShouldResetRenderStateForGpuWarmup(true),
           "Era51: skip GPU reset when coop prepared spawn");
    Expect(ShouldResetRenderStateForGpuWarmup(false),
           "Era51: reset GPU when spawn not coop-prepared");
    Expect(!ShouldWarmupGreedyGpuDuringEnter(10, true, 0, 3),
           "Era51: no early GPU draw before min frames");
    Expect(ShouldWarmupGreedyGpuDuringEnter(10, true, 2, 3),
           "Era51: early GPU draw when coop exits warmup early");
    Expect(ShouldWarmupGreedyGpuDuringEnter(1, false, 23, 3),
           "Era51: legacy last-frame GPU draw");
  }

  // --- Era44b/Era45 enter warmup status + R4 ownership ---
  {
    using cutum::BuildEnterWarmupStatus;
    using cutum::ClassifyRemeshAfterLitApply;
    using cutum::EnterWarmupStatusPrefersMeshOverFifo;
    using cutum::RemeshAfterLitApplyDecision;
    using cutum::ShouldSuppressRelightSeamDirtyForEnterGate;
    cutum::EnterLitSample sample{};
    sample.fifo_n = 10;
    sample.inflight = 5;
    sample.mesh_gpu_pending_near = 15;
    sample.mesh_dirty = true;
    Expect(EnterWarmupStatusPrefersMeshOverFifo(sample, 0, false, false),
           "Era44b: mesh/gpu status beats fifo");
    const std::string mesh_status =
        BuildEnterWarmupStatus(sample, 0, false, false, 5000.0, 120000);
    Expect(mesh_status.rfind("Building terrain", 0) == 0,
           "Era44b: gpu_pending shows Building terrain");
    cutum::EnterLitSample lit_only{};
    lit_only.fifo_n = 8;
    lit_only.inflight = 3;
    const std::string lit_status =
        BuildEnterWarmupStatus(lit_only, 2, true, false, 1000.0, 120000);
    Expect(lit_status.rfind("Lighting queue", 0) == 0,
           "Era44b: fifo-only shows Lighting queue");
    Expect(ClassifyRemeshAfterLitApply(true, false, false, false) ==
               RemeshAfterLitApplyDecision::SkipAlreadyDirty,
           "Era45: dirty ⇒ skip RAA");
    Expect(ClassifyRemeshAfterLitApply(false, true, false, false) ==
               RemeshAfterLitApplyDecision::SkipAlreadyRaa,
           "Era45: raa pending ⇒ skip");
    Expect(ClassifyRemeshAfterLitApply(false, false, true, false) ==
               RemeshAfterLitApplyDecision::PreferKickGpu,
           "Era45: gpu pending ⇒ PreferKick");
    Expect(ClassifyRemeshAfterLitApply(false, false, false, true) ==
               RemeshAfterLitApplyDecision::SkipInflight,
           "Era45: inflight ⇒ skip");
    Expect(ClassifyRemeshAfterLitApply(false, false, false, false) ==
               RemeshAfterLitApplyDecision::Schedule,
           "Era45: clear ⇒ Schedule");
    Expect(!ShouldSuppressRelightSeamDirtyForEnterGate(true, false, true),
           "Era45 B5: enter gate !ring ⇒ no suppress");
    Expect(ShouldSuppressRelightSeamDirtyForEnterGate(true, true, true),
           "Era45 B5: ring ready ⇒ keep base suppress");
    using cutum::ColumnHasRemeshOwner;
    using cutum::ShouldEnqueueRemeshSeamAfterLit;
    Expect(ColumnHasRemeshOwner(false, true, false, false),
           "cruise: RAA owns remesh");
    Expect(!ShouldEnqueueRemeshSeamAfterLit(true, false, true, false),
           "cruise: drawable ⇒ no RemeshSeam");
    Expect(ShouldEnqueueRemeshSeamAfterLit(true, false, false, false),
           "cruise: undrawn hole may RemeshSeam");
    Expect(!ShouldEnqueueRemeshSeamAfterLit(true, false, false, true),
           "cruise: owned ⇒ no RemeshSeam");
    using cutum::ClampCaptureMovingBgCapWithHoles;
    using cutum::EffectiveRelightCaptureBandCy;
    Expect(ClampCaptureMovingBgCapWithHoles(8, true, true, 2) == 2,
           "cruise B: holes clamp bg to dynamic");
    Expect(EffectiveRelightCaptureBandCy(4, true, true) == 3,
           "cruise B: moving holes narrow band");
    Expect(EffectiveRelightCaptureBandCy(1, true, true) == 1,
           "cruise B: band floor 1");
    using cutum::ShouldSkipRelightOnTrustedDiskLight;
    using cutum::ShouldTrustDiskLightmap;
    Expect(ShouldTrustDiskLightmap(true, true, false),
           "cruise D: disk light trusted");
    Expect(!ShouldTrustDiskLightmap(true, false, false),
           "cruise D: incomplete not trusted");
    Expect(ShouldSkipRelightOnTrustedDiskLight(true),
           "cruise D: skip enqueue when trusted");
    using cutum::ShouldSetLitReadyOnTrustedDisk;
    Expect(!ShouldSetLitReadyOnTrustedDisk(false, false),
           "flicker: no LitReady without lit drawable");
    Expect(!ShouldSetLitReadyOnTrustedDisk(true, true),
           "flicker: no LitReady while remesh in flight");
    Expect(ShouldSetLitReadyOnTrustedDisk(true, false),
           "flicker: LitReady after non-FullyDark drawable settle");
  }

  // --- Era46 enter warmup drain parity / RAA commit coalesce ---
  {
    using cutum::EnterWarmupDrainUsesGpuExplicitPath;
    using cutum::EnterWarmupMeshBudgetDefault;
    using cutum::EnterWarmupRingBlockerLabel;
    using cutum::ShouldEscalateEnterWarmupGpuDrain;
    using cutum::ShouldMarkDirtyAfterRemeshAfterApplyCommit;
    using cutum::ShouldPreferKickAfterRemeshAfterApplyCommit;
    Expect(EnterWarmupMeshBudgetDefault() == 8,
           "Era46: default mesh budget matches Application");
    Expect(EnterWarmupDrainUsesGpuExplicitPath(true),
           "Era46: explicit GPU path when mesh warmup needed");
    Expect(!EnterWarmupDrainUsesGpuExplicitPath(false),
           "Era46: no explicit GPU path when blockers clear");
    Expect(ShouldPreferKickAfterRemeshAfterApplyCommit(true),
           "Era46: PreferKick when gpu pending after RAA erase");
    Expect(!ShouldMarkDirtyAfterRemeshAfterApplyCommit(false, true),
           "Era46: no MarkDirty when gpu pending after RAA erase");
    Expect(ShouldMarkDirtyAfterRemeshAfterApplyCommit(false, false),
           "Era46: MarkDirty when clear after RAA erase");
    Expect(!ShouldMarkDirtyAfterRemeshAfterApplyCommit(true, false),
           "Era46: skip MarkDirty if already dirty");
    Expect(!ShouldEscalateEnterWarmupGpuDrain(false, 200000.0),
           "Era46: no escalate without abort_drain");
    Expect(!ShouldEscalateEnterWarmupGpuDrain(true, 100000.0),
           "Era46: no escalate before 3 min");
    Expect(ShouldEscalateEnterWarmupGpuDrain(true, 180000.0),
           "Era46: escalate after abort_drain ≥3 min");
    Expect(std::string(EnterWarmupRingBlockerLabel(true, 5, true, false)) ==
               "dirty",
           "Era46: ring_blocker prefers dirty");
    Expect(std::string(EnterWarmupRingBlockerLabel(false, 5, true, false)) ==
               "gpu",
           "Era46: ring_blocker gpu when no dirty");
  }

  // --- Era47 enter lit quiesce / PreferKick-only / admission ---
  {
    using cutum::ClassifyRemeshAfterLitApply;
    using cutum::ComputeMeshWorkAdmission;
    using cutum::EnterVisibilityReadyRadiusChunks;
    using cutum::EnterVisibilityVoidNearMax;
    using cutum::EnterVisibilityVoidReady;
    using cutum::EnterVoidExitMax;
    using cutum::IsEnterGpuWarmupReady;
    using cutum::MeshWorkAdmission;
    using cutum::MeshWorkAdmissionInput;
    using cutum::RemeshAfterLitApplyDecision;
    using cutum::ShouldMarkDirtyAfterRemeshAfterApplyCommit;
    using cutum::ShouldSuppressMarkRelitRemeshOnEnterLitQuiesce;
    Expect(!ShouldSuppressMarkRelitRemeshOnEnterLitQuiesce(false, true),
           "no suppress without enter gate");
    Expect(!ShouldSuppressMarkRelitRemeshOnEnterLitQuiesce(true, false),
           "no suppress while column not enter-settled");
    Expect(ShouldSuppressMarkRelitRemeshOnEnterLitQuiesce(true, true, 3),
           "fifo residual does not block suppress when column settled");
    Expect(ShouldSuppressMarkRelitRemeshOnEnterLitQuiesce(true, true),
           "suppress MarkRelit remesh only when column enter-settled");
    // Latch semantics covered in World EnterLitQuiesceLatched (fifo blips).
    Expect(ClassifyRemeshAfterLitApply(false, false, false, false, true, false,
                                       /*visual_ready=*/true) ==
               RemeshAfterLitApplyDecision::SkipEnterLitQuiesce,
           "Era49: VisualReady under quiesce ⇒ Skip");
    Expect(ClassifyRemeshAfterLitApply(false, false, false, false, true, false,
                                       /*visual_ready=*/false) ==
               RemeshAfterLitApplyDecision::Schedule,
           "Era49: not VisualReady under quiesce ⇒ Schedule");
    Expect(ClassifyRemeshAfterLitApply(false, false, false, false, true,
                                       /*fully_dark=*/true, false,
                                       /*light_delta=*/true) ==
               RemeshAfterLitApplyDecision::Schedule,
           "FullyDark under quiesce + light delta ⇒ Schedule");
    Expect(ClassifyRemeshAfterLitApply(false, false, false, false, true,
                                       /*fully_dark=*/true, false,
                                       /*light_delta=*/false) ==
               RemeshAfterLitApplyDecision::SkipEnterLitQuiesce,
           "FullyDark under quiesce without delta ⇒ no spin remesh");
    Expect(ClassifyRemeshAfterLitApply(false, false, true, false, true) ==
               RemeshAfterLitApplyDecision::PreferKickGpu,
           "Era47: gpu under quiesce ⇒ PreferKick");
    Expect(ClassifyRemeshAfterLitApply(false, false, false, false, false) ==
               RemeshAfterLitApplyDecision::Schedule,
           "Era47: clear without quiesce ⇒ Schedule");
    Expect(EnterVisibilityVoidReady(0, 999),
           "Era48: no dark-face sample ⇒ void gate open");
    Expect(EnterVisibilityVoidReady(100, 0),
           "Era51: unfinished void==0 ⇒ ready");
    Expect(!EnterVisibilityVoidReady(100, 1),
           "Era51: unfinished void>0 ⇒ not ready");
    Expect(EnterVisibilityVoidReady(100, 150, EnterVisibilityVoidNearMax()),
           "Era51: cruise OceanHealVoidBias 200 still allows 150");
    Expect(!EnterVisibilityVoidReady(100, 201, EnterVisibilityVoidNearMax()),
           "Era51: cruise bias still rejects >200");
    Expect(EnterVoidExitMax() == 0, "Era51: enter void exit max is 0");
    using cutum::EnterVoidTelemFaceExcluded;
    Expect(EnterVoidTelemFaceExcluded(true, false, true, true, true),
           "Era52: terminal chunk excluded from void telem");
    Expect(EnterVoidTelemFaceExcluded(false, true, true, true, false),
           "Era52: gate Done column excluded from void telem");
    Expect(EnterVoidTelemFaceExcluded(false, false, true, true, true),
           "Era52: LitReady void-edge excluded under enter gate");
    Expect(!EnterVoidTelemFaceExcluded(false, false, true, true, false),
           "Era52: unlit void-edge still counts");
    Expect(!EnterVoidTelemFaceExcluded(false, false, false, true, true),
           "Era52: stale face not excluded by void-edge rule");
    using cutum::ShouldLatchStaleFullyDarkAfterEnterGpuCommit;
    using cutum::ShouldSkipMarkRelitAfterEnterStaleAttempt;
    Expect(ShouldLatchStaleFullyDarkAfterEnterGpuCommit(true, true, true, false),
           "legacy latch helper exists — not SoT for GPU commit/exit");
    Expect(!ShouldLatchStaleFullyDarkAfterEnterGpuCommit(true, true, true, true),
           "legacy latch helper: already terminal");
    Expect(!ShouldLatchStaleFullyDarkAfterEnterGpuCommit(true, true, false, false),
           "legacy latch helper: void-edge not latched");
    Expect(ShouldSkipMarkRelitAfterEnterStaleAttempt(true, true, false),
           "skip MarkRelit after attempt when mesh is no longer stale");
    Expect(!ShouldSkipMarkRelitAfterEnterStaleAttempt(true, true, true),
           "still-stale after attempt ⇒ another Dirty (delta)");
    Expect(!ShouldSkipMarkRelitAfterEnterStaleAttempt(true, false, false),
           "first stale MarkRelit still allowed");
    using cutum::EnterFullyDarkDrawableAcceptedForWarmupExit;
    Expect(EnterFullyDarkDrawableAcceptedForWarmupExit(true, true, false),
           "legacy FullyDark-accept helper is not enter SoT (unused for exit)");
    Expect(EnterFullyDarkDrawableAcceptedForWarmupExit(true, false, true),
           "legacy FullyDark-accept helper is not enter SoT (unused for exit)");
    Expect(!EnterFullyDarkDrawableAcceptedForWarmupExit(true, false, false),
           "legacy helper: non-terminal FullyDark not accepted");
    Expect(!EnterFullyDarkDrawableAcceptedForWarmupExit(false, true, true),
           "legacy helper: lit drawable path unchanged");
    using cutum::EnterSoftDeferBlocksWarmupExit;
    using cutum::EnterVisualWarmupYieldsToGateRemaining;
    Expect(EnterSoftDeferBlocksWarmupExit(true, true, false),
           "SoftDefer underfeet still blocks without terminal");
    Expect(!EnterSoftDeferBlocksWarmupExit(true, true, true),
           "legacy terminal SoftDefer bypass exists — unused for exit");
    Expect(!EnterSoftDeferBlocksWarmupExit(true, false, false),
           "far SoftDefer still not enter FirstMesh");
    Expect(EnterVisualWarmupYieldsToGateRemaining(true, 0, true),
           "remaining==0 + underfeet present yields visual warmup");
    Expect(!EnterVisualWarmupYieldsToGateRemaining(true, 0, false),
           "remaining==0 alone does not yield without underfeet");
    Expect(!EnterVisualWarmupYieldsToGateRemaining(true, 3, true),
           "remaining>0 keeps visual warmup");
    Expect(!EnterVisualWarmupYieldsToGateRemaining(false, 0, true),
           "no enter gate keeps visual warmup");
    Expect(EnterVisibilityReadyRadiusChunks(8) == 8,
           "Era48: visibility radius = RD");
    Expect(IsEnterGpuWarmupReady(true, 0, true, true, true),
           "Era48: ready when visibility ok");
    Expect(!IsEnterGpuWarmupReady(true, 0, true, true, false),
           "Era48: not ready while visibility debt");
    // Latch semantics covered in World EnterLitQuiesceLatched (fifo blips).
    Expect(!ShouldMarkDirtyAfterRemeshAfterApplyCommit(false, false, true,
                                                       /*needs_first_mesh=*/false),
           "Era47 P3: enter gate drawable ⇒ no RAA MarkDirty");
    Expect(ShouldMarkDirtyAfterRemeshAfterApplyCommit(false, false, true,
                                                      /*needs_first_mesh=*/true),
           "sky-fix: enter gate !Drawable ⇒ RAA MarkDirty FirstMesh");
    Expect(ShouldMarkDirtyAfterRemeshAfterApplyCommit(
               false, false, true, /*needs_first_mesh=*/false,
               /*fully_dark_drawable=*/true),
           "123647: enter FullyDark drawable ⇒ RAA MarkDirty");
    Expect(ShouldMarkDirtyAfterRemeshAfterApplyCommit(false, false, false),
           "Era47: outside enter MarkDirty still allowed when clear");
    MeshWorkAdmissionInput enter_in{};
    enter_in.pending_gpu = 4;
    enter_in.enter_lit_gate = true;
    enter_in.ring_depth = 8;
    const auto enter_adm = ComputeMeshWorkAdmission(enter_in);
    Expect(enter_adm.mode != MeshWorkAdmission::Mode::Normal,
           "Era47 P2: enter gate never Normal admission");
  }

  // --- Era49 Strict Enter VisualReady invariants (pure) ---
  {
    using cutum::ColumnQuiesceLatchAloneIsVisualReady;
    using cutum::ColumnScheduleAloneIsVisualReady;
    using cutum::ColumnVisualReadyFromFlags;
    using cutum::EnterGateBlocksRaaMarkDirty;
    using cutum::ShouldHideFullyDarkUntilLitInRing;
    using cutum::ShouldMarkDirtyAfterRemeshAfterApplyCommit;
    using cutum::StrictEnterVisualReadyDefault;
    Expect(StrictEnterVisualReadyDefault(),
           "Era49 P0: StrictEnterVisualReady default on");
    Expect(!ColumnScheduleAloneIsVisualReady(true),
           "Era49 P0: Sticky/schedule alone ⇏ VisualReady");
    Expect(!ColumnQuiesceLatchAloneIsVisualReady(true),
           "Era49 P0: Quiesce latch alone ⇏ VisualReady");
    Expect(ColumnVisualReadyFromFlags(/*terrain*/ true, /*pending*/ false,
                                      /*lit*/ true, /*fully_dark*/ false,
                                      /*missing*/ false, /*soft_no_ticket*/ false),
           "Era49 P0: lit terrain column ready");
    Expect(!ColumnVisualReadyFromFlags(true, false, true, /*fully_dark*/ true,
                                       false, false, /*sticky*/ true),
           "Era49 P0: FullyDark + Sticky ⇏ ready");
    Expect(!ColumnVisualReadyFromFlags(true, false, true, false, false,
                                       /*soft_defer_empty*/ true),
           "SoftDefer empty ⇏ VisualReady");
    Expect(ColumnVisualReadyFromFlags(/*terrain*/ false, false, true, false,
                                       false, false),
           "Era49b: missing terrain band = N/A ready (not debt)");
    Expect(ShouldHideFullyDarkUntilLitInRing(8, true, false, 8),
           "legacy hide helper at horiz==ring");
    Expect(!ShouldHideFullyDarkUntilLitInRing(5, true, false, 4),
           "hide ring stays 4 (horiz 5 not hidden)");
    Expect(!EnterGateBlocksRaaMarkDirty(false, false),
           "Era49b: no enter ⇒ RAA MarkDirty allowed");
    Expect(EnterGateBlocksRaaMarkDirty(false, true),
           "Era49b: EnterGpuQuiesceDrain blocks RAA MarkDirty");
    Expect(!ShouldMarkDirtyAfterRemeshAfterApplyCommit(false, false, true,
                                                       /*needs_first_mesh=*/false),
           "Era49b: enter gate drawable ⇒ no RAA MarkDirty");
    Expect(ShouldMarkDirtyAfterRemeshAfterApplyCommit(false, false, true,
                                                      /*needs_first_mesh=*/true),
           "sky-fix: enter !Drawable still MarkDirty");
    Expect(ShouldMarkDirtyAfterRemeshAfterApplyCommit(
               false, false, true, false, /*fully_dark_drawable=*/true),
           "123647: enter FullyDark still MarkDirty");
  }

  // --- Era50 EnterVisualGate completion FSM (pure) ---
  {
    using cutum::AdvanceEnterVisualItemStateMonotonic;
    using cutum::ClassifyEnterVoidEdgeAction;
    using cutum::ClassifyEnterVisualItemState;
    using cutum::EnterLitQuiesceAllowed;
    using cutum::EnterGpuQuiesceDrainAllowed;
    using cutum::EnterVisibilityUnfinishedVoid;
    using cutum::EnterVisualItemState;
    using cutum::EnterVisualVoidEdgeAcceptsSoftDefer;
    using cutum::EnterVoidEdgeAction;
    using cutum::ShouldEscalateEnterWorklistGpuDrain;
    using cutum::ShouldTreatMissingNeighborAsOpenSky;
    Expect(EnterGpuQuiesceDrainAllowed(true),
           "Era50: GpuQuiesceDrain on for whole gate");
    Expect(!EnterLitQuiesceAllowed(true, 5),
           "Era50: LitQuiesce off while remaining>0");
    Expect(EnterLitQuiesceAllowed(true, 0),
           "Era50: LitQuiesce only when remaining==0");
    Expect(!EnterLitQuiesceAllowed(false, 0),
           "Era50: LitQuiesce off without gate");
    Expect(EnterVisibilityUnfinishedVoid(985, 800) == 185,
           "Era50: unfinished void excludes SoftDefer placeholders");
    Expect(EnterVisibilityUnfinishedVoid(100, 200) == 0,
           "Era50: unfinished void floors at 0");
    Expect(EnterVisualVoidEdgeAcceptsSoftDefer(true, false, true, false, true),
           "Era50: void-edge SoftDefer+ticket accepted");
    Expect(!EnterVisualVoidEdgeAcceptsSoftDefer(true, false, true, true, true),
           "Era50: stale FullyDark not SoftDefer terminal");
    Expect(ClassifyEnterVoidEdgeAction(true, true, false, false, false) ==
               EnterVoidEdgeAction::RelightOnce,
           "stale FullyDark without OpenSky ⇒ RelightOnce first");
    Expect(ClassifyEnterVoidEdgeAction(true, false, false, false, false) ==
               EnterVoidEdgeAction::RelightOnce,
           "void-edge without OpenSky ⇒ RelightOnce (not SoftDefer/Done)");
    Expect(ClassifyEnterVoidEdgeAction(true, true, false, false, true) ==
               EnterVoidEdgeAction::RemeshStale,
           "stale after OpenSky ⇒ remesh once");
    Expect(ClassifyEnterVoidEdgeAction(true, false, false, true, true) ==
               EnterVoidEdgeAction::None,
           "void-edge relight inflight ⇒ wait Apply");
    Expect(ClassifyEnterVoidEdgeAction(true, false, false, true, false) ==
               EnterVoidEdgeAction::RelightOnce,
           "OpenSky still required while relight is already owned");
    Expect(ClassifyEnterVisualItemState(true, false, false, false) ==
               EnterVisualItemState::NeedLight,
           "Era50: pending ⇒ NeedLight");
    Expect(ClassifyEnterVisualItemState(false, true, false, false) ==
               EnterVisualItemState::NeedRemesh,
           "Era50: stale dark ⇒ NeedRemesh");
    Expect(ClassifyEnterVisualItemState(false, false, true, false) ==
               EnterVisualItemState::NeedGpu,
           "Era50: gpu busy ⇒ NeedGpu");
    Expect(ClassifyEnterVisualItemState(false, false, false, true) ==
               EnterVisualItemState::Done,
           "terminal_ready (lit or true-dark) ⇒ Done");
    Expect(ClassifyEnterVisualItemState(false, false, false, false) ==
               EnterVisualItemState::NeedRemesh,
           "SoftDefer is not Done");
    Expect(AdvanceEnterVisualItemStateMonotonic(EnterVisualItemState::Done,
                                                EnterVisualItemState::NeedLight) ==
               EnterVisualItemState::Done,
           "Era50: Done sticky (monotonic debt)");
    Expect(AdvanceEnterVisualItemStateMonotonic(EnterVisualItemState::Done,
                                                EnterVisualItemState::Done) ==
               EnterVisualItemState::Done,
           "SoT: Done stays Done when still settled");
    Expect(ShouldEscalateEnterWorklistGpuDrain(true, 10, 3, 90),
           "Era50: escalate GPU when worklist stall + pending");
    Expect(!ShouldEscalateEnterWorklistGpuDrain(true, 10, 3, 10),
           "Era50: no escalate before stall frames");
    Expect(ShouldTreatMissingNeighborAsOpenSky(false, true, true),
           "Era51: missing neighbor under enter ⇒ OpenSky");
    Expect(!ShouldTreatMissingNeighborAsOpenSky(false, false, true),
           "Era51: OpenSky off outside enter gate");
    Expect(!ShouldTreatMissingNeighborAsOpenSky(true, true, true),
           "Era51: loaded neighbor ⇒ no OpenSky inject");
    Expect(ClassifyEnterVoidEdgeAction(true, true, false, false, false) ==
               EnterVoidEdgeAction::RelightOnce,
           "Era51b: stale without OpenSky ⇒ RelightOnce");
  }

  // --- Enter column pipeline SoT (worklist r=4, remesh-if-delta) ---
  {
    using cutum::ClassifyEnterVisualItemState;
    using cutum::EnterFullyDarkColumnSettled;
    using cutum::EnterUnderfeetPresentReady;
    using cutum::EnterUnderfeetSliceReady;
    using cutum::EnterVisualItemState;
    using cutum::EnterVisualWorkRadiusChunks;
    using cutum::IsEnterGpuWarmupReady;
    using cutum::ShouldHideEnterFullyDark;
    using cutum::ShouldHideFullyDarkUntilLitInRing;
    using cutum::FirstMeshPruneKeepHoriz;
    using cutum::ShouldHideUncomputedFullyDarkInRing;
    using cutum::ShouldRemeshAfterLightApply;
    using cutum::ShouldSkipSpawnMeshWhileRelightDeferred;
    using cutum::ShouldSpinFullyDarkRemesh;
    Expect(EnterVisualWorkRadiusChunks() == 4, "enter work radius is 4");
    Expect(ShouldSpinFullyDarkRemesh(true, false, false),
           "FullyDark + no stale + no delta ⇒ spin (do not Schedule)");
    Expect(!ShouldSpinFullyDarkRemesh(true, false, true),
           "light delta ⇒ not a FullyDark spin");
    Expect(ShouldRemeshAfterLightApply(true), "remesh after light apply");
    Expect(!ShouldRemeshAfterLightApply(false), "no remesh without delta");
    Expect(!EnterFullyDarkColumnSettled(false, false, true, false, false),
           "OpenSky alone ≠ settled");
    Expect(!EnterFullyDarkColumnSettled(true, false, true, true, false),
           "OpenSky + stale ≠ settled");
    Expect(EnterFullyDarkColumnSettled(true, false, true, false, false),
           "OpenSky + !stale (true-dark) = settled");
    Expect(EnterFullyDarkColumnSettled(false, false, true, true, true),
           "lit drawable = settled");
    Expect(!EnterFullyDarkColumnSettled(true, true, true, false, false),
           "pending ⇒ not settled");
    using cutum::EnterLitSnapshotResolvedByWorklistDone;
    using cutum::EnterLitSnapshotResolvedByStickyRemesh;
    using cutum::ShouldForceUnderfeetSolidFirstMeshDirty;
    Expect(EnterLitSnapshotResolvedByWorklistDone(true, true, true),
           "worklist Done resolves snapshot debt");
    Expect(!EnterLitSnapshotResolvedByWorklistDone(true, true, false),
           "unfinished worklist ≠ snapshot resolved");
    Expect(EnterLitSnapshotResolvedByStickyRemesh(true, true, false, true),
           "sticky remesh + lit resolves snapshot");
    Expect(!EnterLitSnapshotResolvedByStickyRemesh(true, true, true, true),
           "sticky + pending ≠ resolved");
    Expect(ShouldForceUnderfeetSolidFirstMeshDirty(false, true, false, false,
                                                   false, false, false),
           "orphan solid SoftDefer empty may force Dirty");
    Expect(!ShouldForceUnderfeetSolidFirstMeshDirty(false, true, true, false,
                                                    false, false, false),
           "already Dirty ⇒ no underfeet force");
    Expect(!ShouldForceUnderfeetSolidFirstMeshDirty(false, true, false, true,
                                                    false, false, false),
           "SoftDeferHeld ⇒ no underfeet force");
    using cutum::EnterLitQuiesceKeepSpawnUndrawnDirty;
    Expect(EnterLitQuiesceKeepSpawnUndrawnDirty(true, false, 0),
           "spawn SoftDefer empty keeps Dirty under quiesce");
    Expect(EnterLitQuiesceKeepSpawnUndrawnDirty(true, false, 2),
           "spawn r=2 undrawn keeps Dirty");
    Expect(!EnterLitQuiesceKeepSpawnUndrawnDirty(true, false, 3),
           "outside spawn parks SoftDefer empty");
    Expect(!EnterLitQuiesceKeepSpawnUndrawnDirty(true, true, 0),
           "drawable not kept as undrawn Dirty");
    using cutum::EnterLitQuiesceLiftSpawnSoftDefer;
    Expect(EnterLitQuiesceLiftSpawnSoftDefer(true, 0),
           "quiesce lifts SoftDefer underfeet");
    Expect(EnterLitQuiesceLiftSpawnSoftDefer(true, 2),
           "quiesce lifts SoftDefer spawn r=2");
    Expect(!EnterLitQuiesceLiftSpawnSoftDefer(true, 3),
           "quiesce keeps SoftDefer outside spawn");
    Expect(!EnterLitQuiesceLiftSpawnSoftDefer(false, 0),
           "no quiesce ⇒ SoftDefer policy unchanged");
    using cutum::EnterSpawnPresentableCyRange;
    using cutum::EnterSpawnRingIgnoresHinterlandMeshDebt;
    int cy0 = 0;
    int cy1 = 0;
    EnterSpawnPresentableCyRange(/*player*/ 6, /*sea*/ 4, true, 16, cy0, cy1);
    Expect(cy0 <= 6 && cy1 >= 6, "presentable band covers player cy");
    Expect(cy0 < 6 || cy0 == 5, "band includes below player");
    Expect(EnterSpawnRingIgnoresHinterlandMeshDebt(true, 0, true),
           "Done+underfeet ignores hinterland mesh debt");
    Expect(!EnterSpawnRingIgnoresHinterlandMeshDebt(true, 1, true),
           "visibility debt keeps full ring");
    Expect(ShouldHideEnterFullyDark(true, false, true, false, false),
           "hide: FullyDark+stale after OpenSky still hidden");
    Expect(!ShouldHideEnterFullyDark(true, false, false, false, true),
           "true-dark not hidden");
    Expect(!ShouldHideEnterFullyDark(true, false, false, true, false),
           "lit drawable not hidden");
    Expect(EnterUnderfeetSliceReady(false, false, true),
           "cave true-dark underfeet is ready");
    Expect(!EnterUnderfeetSliceReady(false, true, true),
           "pending light ⇒ underfeet not ready");
    Expect(EnterUnderfeetSliceReady(true, false, false),
           "lit drawable underfeet is ready");
    Expect(EnterUnderfeetPresentReady(true, true),
           "underfeet present needs opaque draw");
    Expect(!EnterUnderfeetPresentReady(true, false),
           "slice ready without opaque ≠ present");
    Expect(IsEnterGpuWarmupReady(true, 0, true, true, true),
           "exit ready without void==0 in the predicate");
    Expect(!ShouldHideFullyDarkUntilLitInRing(5, true, false, 4),
           "hide r=4 does not hide horiz 5");
    Expect(ShouldHideUncomputedFullyDarkInRing(4, true, true, false),
           "pending light FullyDark hidden in r=4");
    Expect(ShouldHideUncomputedFullyDarkInRing(4, true, false, true),
           "stale FullyDark hidden in r=4");
    Expect(ShouldHideUncomputedFullyDarkInRing(4, true, false, false),
           "FullyDark without true_dark flag stays hidden");
    Expect(!ShouldHideUncomputedFullyDarkInRing(4, true, false, false, 4, true),
           "baked true-dark draws (not hidden)");
    Expect(!ShouldHideUncomputedFullyDarkInRing(5, true, true, true),
           "uncomputed hide stays r=4");
    Expect(ShouldHideUncomputedFullyDarkInRing(1, true, true, false),
           "Era28: nh1 FullyDark hidden until lit (no Unlit-near preview)");
    Expect(FirstMeshPruneKeepHoriz(5) >= 4,
           "never prune FirstMesh inside LitDrawable ring");
    Expect(ClassifyEnterVisualItemState(false, false, false, true) ==
               EnterVisualItemState::Done,
           "OpenSky≠Done: terminal_ready only for lit/true-dark");
    Expect(ClassifyEnterVisualItemState(false, false, false, false) !=
               EnterVisualItemState::Done,
           "OpenSky≠Done: SoftDefer is not Done");
    Expect(ShouldSkipSpawnMeshWhileRelightDeferred(true, 4),
           "deferred relight parks spawn r=4 mesh");
    Expect(!ShouldSkipSpawnMeshWhileRelightDeferred(true, 5),
           "hinterland MeshWarmup continues while deferred");
    Expect(!ShouldSkipSpawnMeshWhileRelightDeferred(false, 1),
           "after deferred=false spawn may mesh");
  }

  // --- Era34 CreateBar debt / soft wall ---
  {
    using cutum::CreateBarDebtFraction;
    using cutum::CreateNearFovSoftDeferRadiusChunks;
    using cutum::CreateSpawnWarmupHardWallMs;
    using cutum::CreateSpawnWarmupMaxTicks;
    using cutum::CreateSpawnWarmupSoftWallMs;
    using cutum::ShouldHardLeaveCreateSpawnWarmup;
    using cutum::ShouldSoftLeaveCreateSpawnWarmup;

    Expect(CreateNearFovSoftDeferRadiusChunks() == 2,
           "Era34 P0: near-FOV SoftDefer radius 2");
    Expect(CreateSpawnWarmupSoftWallMs() == 12000, "Era34 P0: soft wall 12s");
    Expect(CreateSpawnWarmupHardWallMs() == 20000, "Era34 P0: hard wall 20s");
    Expect(CreateSpawnWarmupMaxTicks() == 360, "Era34 P0: max ticks 360");
    Expect(CreateBarDebtFraction(10, 10) == 1.0f,
           "Era34 P0: full debt ⇒ fraction 1");
    Expect(CreateBarDebtFraction(0, 10) == 0.0f,
           "Era34 P0: zero debt ⇒ fraction 0");
    Expect(CreateBarDebtFraction(5, 10) < CreateBarDebtFraction(8, 10),
           "Era34 P0: debt↓ ⇒ fraction↓ (monotonic)");
    const float p_hi = 1.0f - CreateBarDebtFraction(2, 10);
    const float p_lo = 1.0f - CreateBarDebtFraction(8, 10);
    Expect(p_hi > p_lo, "Era34 P0: debt↓ ⇒ progress↑");
    Expect(!ShouldSoftLeaveCreateSpawnWarmup(false, 15000.0),
           "Era34 P0: soft wall requires underfeet lit");
    Expect(ShouldSoftLeaveCreateSpawnWarmup(true, 12000.0),
           "Era34 P0: soft wall after underfeet + 12s");
    Expect(!ShouldSoftLeaveCreateSpawnWarmup(true, 5000.0),
           "Era34 P0: soft wall not before 12s");
    Expect(ShouldHardLeaveCreateSpawnWarmup(20000.0, 1),
           "Era34 P0: hard wall 20s");
    Expect(ShouldHardLeaveCreateSpawnWarmup(1000.0, 360),
           "Era34 P0: hard leave on tick ceiling");
    Expect(!ShouldHardLeaveCreateSpawnWarmup(1000.0, 100),
           "Era34 P0: no hard leave early");
    // Near settle ≠ ring4: SoftDefer at horiz=3 is outside create SoftDefer r≤2.
    Expect(CreateNearFovSoftDeferRadiusChunks() < kVisualStageLitDrawableHoriz,
           "Era34 P0: near settle radius < LitDrawable ring4");
  }

  // --- Era36 B1 surface band clamp ---
  {
    using cutum::RelightSurfaceBandCy;
    using cutum::RelightSurfaceBandMaxY;
    using cutum::RelightSurfaceBandMinY;
    Expect(RelightSurfaceBandMinY(64, 16, 0) == 48,
           "Era36 B1: surface_y=64 clamps min_y from 0 to 48");
    Expect(RelightSurfaceBandMinY(16, 16, 0) == 0,
           "Era36 B1: surface_y=16 clamps to 0");
    Expect(RelightSurfaceBandMinY(64, 16, 60) == 60,
           "Era36 B1: original min_y=60 > surface_min=48, keeps 60");
    Expect(RelightSurfaceBandMaxY(64, 16, 255, 255) == 127,
           "Era36 B1: surface_y=64 caps max_y at cy7 top (127)");
    Expect(RelightSurfaceBandMaxY(64, 16, 255, 100) == 100,
           "Era36 B1: original max below band top keeps original");
    const auto band = RelightSurfaceBandCy(64, 16, 15);
    Expect(band.first == 3 && band.second == 7,
           "Era36 B1: surface cy=4 -> band 3..7");
  }

  // --- Era36 B2 / Era40 dynamic capture cap (threshold 15) ---
  {
    using cutum::DynamicCaptureMovingBgCap;
    Expect(DynamicCaptureMovingBgCap(5) == 1,
           "Era36 B2: low pending -> base cap 1");
    Expect(DynamicCaptureMovingBgCap(25) == 3,
           "Era36 B2: pending=25 -> cap 3");
    Expect(DynamicCaptureMovingBgCap(50) == 4,
           "Era36 B2: pending=50 -> cap clamped to 4");
    Expect(DynamicCaptureMovingBgCap(15) == 1,
           "Era40: pending=15 (threshold) -> base cap");
    Expect(DynamicCaptureMovingBgCap(16) == 2,
           "Era40: pending=16 -> cap 2");
  }

  // --- Era36 B3 / Era40 land moving drain (threshold 15) ---
  {
    using cutum::LandMovingRelightDrainFloor;
    using cutum::ShouldDrainPendingLightLandMoving;
    Expect(!ShouldDrainPendingLightLandMoving(10),
           "Era40: low pending -> no drain");
    Expect(!ShouldDrainPendingLightLandMoving(15),
           "Era40: pending=15 (threshold) -> no drain");
    Expect(ShouldDrainPendingLightLandMoving(16),
           "Era40: pending=16 -> drain");
    Expect(ShouldDrainPendingLightLandMoving(50),
           "Era40: high pending -> drain");
    Expect(LandMovingRelightDrainFloor(true, 10) == 0,
           "Closeout F: land drain floor folded away");
    Expect(LandMovingRelightDrainFloor(true, 16) == 0,
           "Closeout F: land drain floor always 0");
    Expect(LandMovingRelightDrainFloor(false, 50) == 0,
           "Closeout F: land drain floor 0 when idle");
  }

  // --- Era38 A0 near-FOV work score ---
  {
    using cutum::ColumnFlowFirstMeshPriority;
    using cutum::ColumnFlowRelightPriorityUnderMiss;
    using cutum::NearFovWorkScore;
    using cutum::SoftDeferEmptyNearReserveSlots;
    using cutum::StarveHinterlandUnlit;
    Expect(NearFovWorkScore(0, 0.0f) < NearFovWorkScore(2, 0.0f),
           "Era38 A0: underfeet sooner than side horiz2");
    Expect(NearFovWorkScore(1, 1.0f) < NearFovWorkScore(1, 0.0f),
           "Era38 A0: ahead beats side at same horiz");
    Expect(NearFovWorkScore(2, 0.0f) == NearFovWorkScore(2, -1.0f),
           "Era38 A0: behind gets no fwd credit (ties side)");
    Expect(NearFovWorkScore(1, 1.0f) < NearFovWorkScore(0, 0.0f),
           "Era38 A0: Admit mirror — ahead+1 can beat underfeet");
    Expect(NearFovWorkScore(2, 1.0f) < NearFovWorkScore(5, 1.0f),
           "Era38 A0: near ahead beats far ahead");
    Expect(SoftDeferEmptyNearReserveSlots(12) == 6,
           "Era38 A0: reserve half cap capped at 6");
    Expect(SoftDeferEmptyNearReserveSlots(4) == 2,
           "Era38 A0: reserve half of small cap");
    Expect(SoftDeferEmptyNearReserveSlots(1) == 1,
           "Era38 A0: reserve at least 1");
    Expect(ColumnFlowFirstMeshPriority(105, 0, 4) == 109,
           "Era38 A0: underfeet FirstMesh prio boost");
    Expect(ColumnFlowFirstMeshPriority(105, 4, 4) == 105,
           "Era38 A0: rim FirstMesh keeps base");
    Expect(ColumnFlowRelightPriorityUnderMiss(55, 0, 4, true) == 59,
           "Era38 A0: near Relight boost under miss");
    Expect(ColumnFlowRelightPriorityUnderMiss(95, 0, 10, true) == 99,
           "Era38 A0: Relight clamped to 99 under miss");
    Expect(StarveHinterlandUnlit(1, 0),
           "Era38 A0: starve when SoftDefer empty near");
    Expect(StarveHinterlandUnlit(0, 16),
           "Era38 A0: starve when pending debt");
    Expect(!StarveHinterlandUnlit(0, 10),
           "Era38 A0: no starve when clear");
  }

  // --- Era37 P0 unlit hole preview ---
  {
    using cutum::AllowUnlitDrawableUnderLightDebt;
    Expect(!AllowUnlitDrawableUnderLightDebt(20, 5, 4, true, true, false),
           "Era37 P0: fully-dark void rejected");
    Expect(!AllowUnlitDrawableUnderLightDebt(20, 5, 4, false, false, false),
           "Era37 P0: no greedy mesh rejected");
    Expect(!AllowUnlitDrawableUnderLightDebt(20, 5, 4, false, true, true),
           "Era37 P0: underfeet rejected");
    Expect(!AllowUnlitDrawableUnderLightDebt(20, 5, 5, false, true, false),
           "Era37 P0: outside LitDrawable ring rejected");
    Expect(AllowUnlitDrawableUnderLightDebt(20, 5, 4, false, true, false),
           "Era37 P0: pending debt allows ring preview");
    Expect(AllowUnlitDrawableUnderLightDebt(10, 12, 3, false, true, false),
           "Era37 P0: unlit_near threshold allows preview");
    Expect(!AllowUnlitDrawableUnderLightDebt(10, 5, 3, false, true, false),
           "Era37 P0: low debt blocks preview");
  }

  // --- Era37 P1b GPU apply floor ---
  {
    using cutum::LandRelightGpuApplyFloor;
    Expect(LandRelightGpuApplyFloor(50, 10, 3) == 3,
           "Era37 P1b: low pending keeps base");
    Expect(LandRelightGpuApplyFloor(70, 16, 3) == 12,
           "Era40: fifo+pendf>15 boost apply floor to 12");
    Expect(LandRelightGpuApplyFloor(70, 15, 5) == 5,
           "Era40: pendf=15 alone insufficient");
  }

  // --- Era37 P4 enter warmup ownership ---
  {
    using cutum::EnterWarmupSoftDeferOwnershipCap;
    Expect(EnterWarmupSoftDeferOwnershipCap(12, 10, false) == 12,
           "Era37 P4: inactive warmup keeps base");
    Expect(EnterWarmupSoftDeferOwnershipCap(12, 10, true) == 18,
           "Era37 P4: active warmup boosts cap");
    Expect(EnterWarmupSoftDeferOwnershipCap(12, 3, true) == 12,
           "Era37 P4: low empty keeps base");
  }

  // --- Era37 P5 per-column surface band ---
  {
    using cutum::RelightColumnSurfaceBlockY;
    using cutum::RelightSurfaceBandForColumn;
    Expect(RelightColumnSurfaceBlockY(64, 96) == 96,
           "Era37 P5: column top overrides focus");
    Expect(RelightColumnSurfaceBlockY(64, -1) == 64,
           "Era37 P5: fallback to focus when no column top");
    const auto hill = RelightSurfaceBandForColumn(64, 96, 16, 255, 0, 255);
    Expect(hill.first == 80 && hill.second == 159,
           "Era37 P5: hill column band uses column surface");
  }

  // --- SoftDefer empty incremental rim probe ---
  {
    using cutum::SoftDeferEmptyRimCellsPerFrame;
    using cutum::SoftDeferEmptyShouldProbeCell;
    Expect(SoftDeferEmptyShouldProbeCell(1, 2, 0, 100, 0, 48),
           "near horiz always probed");
    Expect(SoftDeferEmptyShouldProbeCell(2, 2, 99, 100, 0, 48),
           "near-r=2 always probed");
    Expect(SoftDeferEmptyShouldProbeCell(5, 2, 10, 100, 0, 48),
           "rim idx in budget probed");
    Expect(!SoftDeferEmptyShouldProbeCell(5, 2, 60, 100, 0, 48),
           "rim idx outside budget skipped");
    Expect(SoftDeferEmptyShouldProbeCell(5, 2, 60, 100, 50, 48),
           "rim rotates with scan_offset");
    Expect(SoftDeferEmptyRimCellsPerFrame(12, 25) >= 48,
           "rim budget at least 48");
  }

  // --- Era35 P1 cy-window ---
  {
    using cutum::SoftDeferCyWindowNearTop;
    Expect(SoftDeferCyWindowNearTop(10, 3, 1) == 10,
           "Era35 P1: near-FOV (horiz=1) gets max_cy");
    Expect(SoftDeferCyWindowNearTop(10, 3, 2) == 10,
           "Era35 P1: near-FOV (horiz=2) gets max_cy");
    Expect(SoftDeferCyWindowNearTop(10, 3, 3) == 5,
           "Era35 P1: far (horiz=3) gets preferred+2");
    Expect(SoftDeferCyWindowNearTop(10, 3, 5) == 5,
           "Era35 P1: far (horiz=5) gets preferred+2");
  }

  // --- Era35 P2 dynamic ownership cap ---
  {
    using cutum::SoftDeferOwnershipCap;
    Expect(SoftDeferOwnershipCap(0) == 12,
           "Era35 P2: zero empty → cap=12");
    Expect(SoftDeferOwnershipCap(48) == 24,
           "Era35 P2: 48 empty → cap=24");
    Expect(SoftDeferOwnershipCap(100) == 24,
           "Era35 P2: 100 empty → cap clamped to 24");
    Expect(SoftDeferOwnershipCap(20) == 17,
           "Era35 P2: 20 empty → cap=17");
  }

  // --- Era35 P4 cruise catch-up ---
  {
    using cutum::CruiseCatchUpEmergeBudgetMs;
    using cutum::CruiseCatchUpOwnershipCap;
    Expect(CruiseCatchUpEmergeBudgetMs(14.0, 10, true) > 14.0,
           "Era35 P4: moving with empty>5 → boosted budget");
    Expect(CruiseCatchUpEmergeBudgetMs(14.0, 3, true) == 14.0,
           "Era35 P4: moving with empty<=5 → no boost");
    Expect(CruiseCatchUpEmergeBudgetMs(14.0, 10, false) == 14.0,
           "Era35 P4: idle → no boost");
    Expect(CruiseCatchUpOwnershipCap(12, 10, true) == 18,
           "Era35 P4: moving with empty>5 → cap 18");
    Expect(CruiseCatchUpOwnershipCap(12, 3, true) == 12,
           "Era35 P4: moving with empty<=5 → cap unchanged");
  }

  // --- Era34 P2 FirstMesh bias ---
  {
    using cutum::ShouldBiasFirstMeshOverRemesh;
    Expect(ShouldBiasFirstMeshOverRemesh(1, false, true),
           "Era34 P2: SoftDefer empty moving ⇒ FM bias");
    Expect(ShouldBiasFirstMeshOverRemesh(0, true, true),
           "Era34 P2: holes moving ⇒ FM bias");
    Expect(!ShouldBiasFirstMeshOverRemesh(1, true, false),
           "Era34 P2: idle ⇒ no FM bias");
  }

  // --- Era33 P1 cy_order ---
  {
    using cutum::BuildMeshCyVisitOrder;
    const auto land = BuildMeshCyVisitOrder(/*cy0=*/0, /*cy1=*/4, /*prefer=*/0,
                                            /*sea=*/2, /*fill_water=*/false);
    Expect(!land.empty() && land[0] == 0, "Era33 P1: land starts at ground");
    Expect(land.size() >= 3 && land[1] == 1, "Era33 P1: land then +1");
    Expect(land.size() >= 4 && land[2] == 2, "Era33 P1: land canopy after ±1");
    const auto ocean = BuildMeshCyVisitOrder(0, 4, /*prefer=*/2, /*sea=*/2,
                                             /*fill_water=*/true);
    Expect(!ocean.empty() && ocean[0] == 2, "Era33 P1: ocean sea/prefer first");
  }

  {
    MeshWorkAdmissionInput in;
    in.pending_gpu = 6;
    in.pending_gpu_queued = 0;
    in.pending_gpu_kicked = 6;
    in.visual_holes = true;
    in.moving = true;
    in.nearest_miss_cy = 3;
    in.nearest_miss_horiz = 4;
    in.ring_depth = 8;
    in.prev_mode = static_cast<uint8_t>(MeshWorkAdmission::Mode::HoleDrain);
    // P0 harness: predicate true; full remesh=0 wire lands in P1.
    Expect(IsMissFirstMeshClass(true, in.nearest_miss_cy, in.nearest_miss_horiz),
           "214034 witness is FirstMesh class");
    in.remesh_queue_n = 0;
    auto out0 = ComputeMeshWorkAdmission(in);
    Expect(out0.remesh_schedule == 0,
           "HoleDrain miss class + empty RemeshQ ⇒ remesh_schedule=0");
    in.remesh_queue_n = 4;
    auto out1 = ComputeMeshWorkAdmission(in);
    Expect(out1.remesh_schedule >= 1,
           "HoleDrain miss class + RemeshQ≠∅ ⇒ remesh_schedule≥1");
  }

  // --- Era40 Relight FIFO miss-rim pin ---
  {
    using cutum::RelightMissPinMaxHoriz;
    using cutum::ShouldBoostRelightDrainUnderFifoMissStarve;
    using cutum::ShouldForceMissColumnFifoEnqueue;
    using cutum::ShouldPreferMissFinalizeBand;
    using cutum::RelightFifoStuckSoftFail;
    Expect(RelightMissPinMaxHoriz() == 4,
           "Era40: miss pin max horiz = LitDrawable ring");
    Expect(ShouldForceMissColumnFifoEnqueue(true, true, false),
           "Era40: force enqueue miss+pending even if not in FIFO");
    Expect(!ShouldForceMissColumnFifoEnqueue(true, true, true),
           "Era40: already in FIFO -> force enqueue false");
    Expect(!ShouldForceMissColumnFifoEnqueue(false, true, false),
           "Era40: no force without miss");
    Expect(!ShouldForceMissColumnFifoEnqueue(true, false, false),
           "Era40: no force without pending/void");
    Expect(ShouldPreferMissFinalizeBand(0),
           "Era40: underfeet prefer finalize");
    Expect(ShouldPreferMissFinalizeBand(4),
           "Era40: rim horiz4 prefer finalize");
    Expect(!ShouldPreferMissFinalizeBand(5),
           "Era40: beyond ring no finalize prefer");
    Expect(ShouldBoostRelightDrainUnderFifoMissStarve(96, 96, 0, true),
           "Era40: soft-cap + completed0 + miss -> boost");
    Expect(!ShouldBoostRelightDrainUnderFifoMissStarve(96, 96, 0, false),
           "Era40: no boost without miss");
    Expect(!ShouldBoostRelightDrainUnderFifoMissStarve(40, 96, 0, true),
           "Era40: no boost below soft-cap");
    Expect(RelightFifoStuckSoftFail(96, 96, 0, 5, true),
           "Era40: fifo stuck soft-fail when completed=0");
    Expect(!RelightFifoStuckSoftFail(96, 96, 2, 5, true),
           "Era40: no soft-fail when completed>0");
  }

  // Cruise wall P0: remesh DirtyAdmit backpressure.
  {
    using cutum::ApplyRemeshAdmitBackpressure;
    using cutum::RemeshAdmitBackpressureInput;
    using cutum::ShouldApplyRemeshAdmitBackpressure;
    RemeshAdmitBackpressureInput green{};
    green.stream_pressure = 0;
    green.fifo_n = 10;
    green.dirty_n = 50;
    Expect(!ShouldApplyRemeshAdmitBackpressure(green),
           "P0: green low fifo/dirty -> no BP");
    RemeshAdmitBackpressureInput red{};
    red.stream_pressure = 2;
    red.fifo_n = 10;
    red.dirty_n = 50;
    Expect(ShouldApplyRemeshAdmitBackpressure(red), "P0: Red -> BP");
    MeshWorkAdmission adm{};
    adm.dirty_admit_budget = 8;
    adm.remesh_schedule = 3;
    adm.allow_neighbor_dirty = true;
    adm.first_mesh_schedule = 6;
    adm.max_schedule = 9;
    red.miss_active = true;
    red.admit_cap_red = 0;
    ApplyRemeshAdmitBackpressure(adm, red);
    Expect(adm.dirty_admit_budget == 0, "P0: Red admit cap 0");
    Expect(adm.remesh_schedule == 1, "P0: BP keeps remesh_schedule >= 1");
    Expect(!adm.allow_neighbor_dirty, "P0: neighbor dirty off");
    red.remesh_queue_n = 40;
    MeshWorkAdmission adm_deep = adm;
    adm_deep.remesh_schedule = 3;
    ApplyRemeshAdmitBackpressure(adm_deep, red);
    Expect(adm_deep.remesh_schedule == 3,
           "100319 tail: deep RemeshQ + miss -> remesh cap 3");
    RemeshAdmitBackpressureInput fifo{};
    fifo.stream_pressure = 1;
    fifo.fifo_n = 72;
    fifo.relight_fifo_soft_cap = 96;
    fifo.fifo_admit_frac = 0.75f;
    fifo.dirty_n = 10;
    fifo.admit_cap_yellow = 1;
    fifo.miss_active = false;
    Expect(ShouldApplyRemeshAdmitBackpressure(fifo),
           "P0: fifo>=0.75 soft-cap -> BP");
    MeshWorkAdmission adm_y{};
    adm_y.dirty_admit_budget = 8;
    adm_y.remesh_schedule = 3;
    ApplyRemeshAdmitBackpressure(adm_y, fifo);
    Expect(adm_y.dirty_admit_budget == 1, "P0: Yellow admit cap 1");
    Expect(adm_y.remesh_schedule == 1, "P0: no-miss remesh_schedule min 1");
  }

  // Cruise wall P4: Red fifo light-drain predicate.
  {
    using cutum::ShouldCruiseRedFifoLightDrain;
    Expect(ShouldCruiseRedFifoLightDrain(2, 72, 96, 0.75f, true, 5),
           "P4: Red+fifo+holes+pendf -> drain");
    Expect(!ShouldCruiseRedFifoLightDrain(1, 72, 96, 0.75f, true, 5),
           "P4: Yellow no Red drain");
    Expect(!ShouldCruiseRedFifoLightDrain(2, 72, 96, 0.75f, true, 0),
           "P4: no pendf -> no drain");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "miss_first_mesh_class_test: OK\n";
  return EXIT_SUCCESS;
}
