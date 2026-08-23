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

  // Skip already dirty
  mesh.ClearCalls();
  mesh.Mut(coord).is_dirty = true;
  in.relit_chunks.clear();
  in.relit_chunks.push_back(SnapshotFromFake(mesh, coord));
  const auto plan2 = PlanColumnInstall(in);
  Expect(plan2.mark_dirty.empty() && plan2.mark_dirty_priority.empty(),
         "skip dirty chunk");

  if (failures != 0)
  {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
