#include "Test/FakeMeshServiceForLitApply.h"
#include "World/Streaming/RelightInstallPlanner.h"

#include <cstdlib>
#include <iostream>

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
  using cutum::ColumnInstallPath;
  using cutum::ExecuteLitApplyPlanOnFake;
  using cutum::FakeMeshCallKind;
  using cutum::FakeMeshServiceForLitApply;
  using cutum::LitApplyColumnInput;
  using cutum::PlanColumnInstall;
  using cutum::SnapshotFromFake;

  FakeMeshServiceForLitApply mesh;
  const glm::ivec3 coord{1, 0, 2};
  mesh.Mut(coord).has_drawable = true;
  mesh.Mut(coord).fully_dark = true;
  mesh.Mut(coord).meshed_light_rev = 1;
  mesh.Mut(coord).light_field_rev = 3;

  LitApplyColumnInput in{};
  in.column = {1, 2};
  in.is_primary = true;
  in.finalize_gate = true;
  in.primary_only = true;
  in.consume_mode = true;
  in.any_drawable = true;
  in.relit_chunks.push_back(SnapshotFromFake(mesh, coord));

  const auto plan = PlanColumnInstall(in);
  Expect(plan.path == ColumnInstallPath::PrimaryConsume,
         "PrimaryConsume plan");
  auto out = ExecuteLitApplyPlanOnFake(mesh, plan);
  Expect(mesh.CountCalls(FakeMeshCallKind::MarkDirtyPriority) >= 1,
         "MarkDirtyPriority on stale dark");
  Expect(out.erased_pending && out.erased_inflight, "FSM clears gates");
  Expect(out.fsm == cutum::ColumnEmergeState::Meshing, "Meshing FSM");

  // Already dirty + light rev ahead → still bump (P6).
  mesh.ClearCalls();
  mesh.Mut(coord).is_dirty = true;
  in.relit_chunks.clear();
  in.relit_chunks.push_back(SnapshotFromFake(mesh, coord));
  const auto plan_stale = PlanColumnInstall(in);
  Expect(!plan_stale.mark_dirty_priority.empty(),
         "P6: dirty FullyDark with light delta still priority");

  // Already dirty + matching revs → skip remesh (P7 GPU-sky noop).
  mesh.ClearCalls();
  mesh.Mut(coord).meshed_light_rev = 3;
  mesh.Mut(coord).light_field_rev = 3;
  in.relit_chunks.clear();
  in.relit_chunks.push_back(SnapshotFromFake(mesh, coord));
  const auto plan_noop = PlanColumnInstall(in);
  Expect(plan_noop.mark_dirty.empty() && plan_noop.mark_dirty_priority.empty(),
         "P7: skip remesh when FullyDark light rev matches");

  // P12 A1: skip_already_dirty on a hole → FirstMesh, not remesh skip.
  {
    FakeMeshServiceForLitApply hole;
    const glm::ivec3 hole_coord{2, 0, 3};
    hole.Mut(hole_coord).is_dirty = true;
    hole.Mut(hole_coord).has_drawable = false;
    hole.Mut(hole_coord).has_greedy = false;
    hole.Mut(hole_coord).fully_dark = false;
    LitApplyColumnInput hole_in{};
    hole_in.column = {2, 3};
    hole_in.is_primary = true;
    hole_in.finalize_gate = true;
    hole_in.primary_only = false;
    hole_in.consume_mode = false;
    hole_in.focus_horiz = 12;
    hole_in.relit_chunks.push_back(SnapshotFromFake(hole, hole_coord));
    const auto plan_fm = PlanColumnInstall(hole_in);
    Expect(plan_fm.path == ColumnInstallPath::PrimaryStandard,
           "P12 A1: Standard path for skip-dirty hole");
    Expect(plan_fm.enqueue_first_mesh, "P12 A1: skip-dirty hole enqueues FM");
    Expect(!plan_fm.mark_dirty_priority.empty(),
           "P12 A1: skip-dirty hole MarkDirtyPriority");
    Expect(plan_fm.skip_already_dirty_n == 0,
           "P12 A1: hole is not skip_already_dirty");

    hole.Mut(hole_coord).has_drawable = true;
    hole.Mut(hole_coord).has_greedy = true;
    hole_in.relit_chunks.clear();
    hole_in.any_drawable = true;
    hole_in.relit_chunks.push_back(SnapshotFromFake(hole, hole_coord));
    const auto plan_lit = PlanColumnInstall(hole_in);
    Expect(!plan_lit.enqueue_first_mesh,
           "P12 A1: drawable lit dirty stays skip (no FM storm)");
  }

  if (failures != 0)
  {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
