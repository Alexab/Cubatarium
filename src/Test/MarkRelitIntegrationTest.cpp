#include "Test/FakeMeshServiceForLitApply.h"
#include "World/Streaming/RelightFifoPolicy.h"
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
  using cutum::EarnedRelightApplyCap;
  using cutum::ExecuteLitApplyPlanOnFake;
  using cutum::FakeMeshCallKind;
  using cutum::FakeMeshServiceForLitApply;
  using cutum::LitApplyColumnInput;
  using cutum::PlanColumnInstall;
  using cutum::ShouldStopRelightApplySlice;
  using cutum::SnapshotFromFake;

  // I01 two relit chunks
  FakeMeshServiceForLitApply mesh;
  const glm::ivec3 c0{0, 0, 0};
  const glm::ivec3 c1{0, 1, 0};
  mesh.Mut(c0).fully_dark = true;
  mesh.Mut(c1).fully_dark = true;
  mesh.Mut(c0).light_field_rev = 2;
  mesh.Mut(c1).light_field_rev = 2;

  LitApplyColumnInput in{};
  in.column = {0, 0};
  in.is_primary = true;
  in.finalize_gate = true;
  in.primary_only = true;
  in.consume_mode = true;
  in.relit_chunks.push_back(SnapshotFromFake(mesh, c0));
  in.relit_chunks.push_back(SnapshotFromFake(mesh, c1));

  const auto plan = PlanColumnInstall(in);
  ExecuteLitApplyPlanOnFake(mesh, plan);
  Expect(mesh.CountCalls(FakeMeshCallKind::MarkDirtyPriority) == 2,
         "I01 two MarkDirtyPriority");

  // I04 cap math light+install
  Expect(EarnedRelightApplyCap(20, 16.0, 0.0, 20.0, true, 0, 3.0, 5.0) >= 2,
         "I04 earned cap >=2 at light+install");
  Expect(!ShouldStopRelightApplySlice(10.0, 1, 16.0, false, true, 3, 9.0, 0, 3.0,
                                     5.0),
         "I04 continue after 1 unit when slim");

  if (failures != 0)
  {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
