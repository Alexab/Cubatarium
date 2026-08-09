#include "World/Streaming/MeshWorkAdmission.h"
#include "World/Streaming/SoftDeferEmptyPolicy.h"

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
  Expect(ShouldReserveVoidRelightSlots(0, 3, true),
         "Era23 I-V4: VB>0 ⇒ Relight slots under miss");
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
  Expect(ShouldForceFirstMeshOnPlaceHole(true, true),
         "Era23 I-P1: empty/undrawn near ⇒ FirstMesh");
  Expect(!ShouldForceFirstMeshOnPlaceHole(true, false),
         "Era23 I-P1: far empty → no force");
  Expect(!ShouldForceFirstMeshOnPlaceHole(false, true),
         "Era23 I-P1: drawable near → no force");

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
