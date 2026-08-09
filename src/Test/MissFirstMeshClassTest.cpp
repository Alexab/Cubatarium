#include "World/Streaming/MeshWorkAdmission.h"
#include "World/Streaming/SoftDeferEmptyPolicy.h"
#include "World/Streaming/AntiFlickerPolicy.h"
#include "World/Streaming/VisualStagePolicy.h"
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
         "no PreferKick without miss");
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
  Expect(!ShouldReserveVoidRelightSlots(0, 3, true),
         "Era23 I-V4: miss + VB remesh-only → no Relight steal");
  Expect(!ShouldReserveVoidRelightSlots(40, 2, false),
         "Era23 I-V4: idle void below T → no Relight reserve");
  Expect(!ShouldReserveVoidRelightSlots(0, 3, false),
         "Era23 I-V4: idle VB remesh-only → no Relight reserve");
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
  Expect(!ShouldEscalateSoftDeferEmptyAge(44),
         "Era24 I-E4: age 44 < sla 45 → no escalate");
  Expect(ShouldEscalateSoftDeferEmptyAge(45),
         "Era24 I-E4: age 45 ≥ sla → escalate");
  Expect(ShouldEscalateSoftDeferEmptyAge(60, 45),
         "Era24 I-E4: age past sla → escalate");
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
  Expect(!IsFrontierPressure(0, 0, true, 999),
         "Era25 I-F4: no gen/async → no frontier pressure");
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

  // --- Era28 Visual Stage Gate ---
  using cutum::ShouldAllowUnlitFirstMeshNearFov;
  using cutum::ShouldPublishMeshToDraw;
  using cutum::ShouldRelightBeforeDrawNear;
  using cutum::ShouldRemeshAfterApplyOnlyWhileBuilding;
  using cutum::SoftDeferEmptyShouldMarkDirty;
  using cutum::kVisualStageNearFovHoriz;

  Expect(kVisualStageNearFovHoriz == 2, "Era28 I-V1: near_r default 2");
  Expect(!ShouldAllowUnlitFirstMeshNearFov(false, true, 1, 2),
         "Era28 I-V1: near horiz⇒ no Unlit");
  Expect(!ShouldAllowUnlitFirstMeshNearFov(false, true, 2, 2),
         "Era28 I-V1: horiz==near_r ⇒ no Unlit");
  Expect(ShouldAllowUnlitFirstMeshNearFov(false, true, 3, 2),
         "Era28 I-V1: far ⇒ Unlit OK");
  Expect(!ShouldAllowUnlitFirstMeshNearFov(true, true, 5, 2),
         "Era28 I-V1: has_mesh ⇒ no Unlit");
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
