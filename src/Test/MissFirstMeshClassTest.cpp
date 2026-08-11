#include "World/Streaming/MeshWorkAdmission.h"
#include "World/Streaming/SoftDeferEmptyPolicy.h"
#include "World/Streaming/AntiFlickerPolicy.h"
#include "World/Streaming/VisualStagePolicy.h"
#include "World/Streaming/CyOrderPolicy.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Streaming/NearFovWorkPriority.h"
#include "World/Streaming/OceanCruisePolicy.h"
#include "World/Streaming/FrontierStagePolicy.h"
#include "World/Streaming/OceanFrontierPolicy.h"

#include <cstdlib>
#include <iostream>

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
  Expect(AsyncScheduleFloorUnderMiss(true) == 12,
         "Era22 I-A1: miss|UV async floor ≥12");
  Expect(AsyncScheduleFloorUnderMiss(false) == 0,
         "Era22 I-A1: calm → no floor");

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
  Expect(EnterVisualWarmupHardCapMs() == 200, "Era30 I-O6: enter hard cap 200ms");
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
  Expect(ShouldForceEnterVisualCap(250.0, false),
         "Era31 I-T4: GpuWarmup elapsed ≥200 ⇒ force cap (load)");
  Expect(ShouldForceEnterVisualCap(50.0, true),
         "Era31 I-T4: mesh soft ready ⇒ force cap");
  Expect(!ShouldForceEnterVisualCap(50.0, false),
         "Era31 I-T4: early GpuWarmup without ready ⇒ no cap");
  Expect(!ShouldForceEnterVisualCap(250.0, false, /*cold_create=*/true),
         "Era33 P0: cold create never force-cap while debt");
  Expect(ShouldForceEnterVisualCap(250.0, true, /*cold_create=*/true),
         "Era33 P0: cold create force when soft-ready");

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

  // --- Era36 B2 dynamic capture cap ---
  {
    using cutum::DynamicCaptureMovingBgCap;
    Expect(DynamicCaptureMovingBgCap(5) == 1,
           "Era36 B2: low pending -> base cap 1");
    Expect(DynamicCaptureMovingBgCap(25) == 3,
           "Era36 B2: pending=25 -> cap 3");
    Expect(DynamicCaptureMovingBgCap(50) == 4,
           "Era36 B2: pending=50 -> cap clamped to 4");
    Expect(DynamicCaptureMovingBgCap(20) == 1,
           "Era36 B2: pending=20 (threshold) -> base cap");
    Expect(DynamicCaptureMovingBgCap(21) == 3,
           "Era36 B2: pending=21 -> cap 3");
  }

  // --- Era36 B3 land moving drain ---
  {
    using cutum::LandMovingRelightDrainFloor;
    using cutum::ShouldDrainPendingLightLandMoving;
    Expect(!ShouldDrainPendingLightLandMoving(10),
           "Era36 B3: low pending -> no drain");
    Expect(!ShouldDrainPendingLightLandMoving(30),
           "Era36 B3: pending=30 (threshold) -> no drain");
    Expect(ShouldDrainPendingLightLandMoving(31),
           "Era36 B3: pending=31 -> drain");
    Expect(ShouldDrainPendingLightLandMoving(50),
           "Era36 B3: high pending -> drain");
    Expect(LandMovingRelightDrainFloor(true, 10) == 0,
           "Era36 B3: floor 0 when pending low");
    Expect(LandMovingRelightDrainFloor(true, 31) == 1,
           "Era36 B3: floor 1 when pending high");
    Expect(LandMovingRelightDrainFloor(false, 50) == 0,
           "Era36 B3: floor 0 when idle");
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
    Expect(LandRelightGpuApplyFloor(70, 25, 3) == 12,
           "Era39 A4: fifo+pending boost apply floor to 12");
    Expect(LandRelightGpuApplyFloor(70, 10, 5) == 5,
           "Era37 P1b: fifo alone insufficient");
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
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "miss_first_mesh_class_test: OK\n";
  return EXIT_SUCCESS;
}
