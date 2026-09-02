#include "World/Streaming/ColumnJobGraph.h"

#include <iostream>

namespace
{
bool Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "column_job_graph_stage_test FAIL: " << msg << '\n';
    return false;
  }
  return true;
}
} // namespace

int main()
{
  using cutum::ColumnJobStage;
  using cutum::DeriveColumnJobStage;

  const ColumnJobStage absent =
      DeriveColumnJobStage(false, false, false, false, false, false);
  if (!Expect(absent == ColumnJobStage::Absent, "absent chunk"))
  {
    return 1;
  }

  const ColumnJobStage pending =
      DeriveColumnJobStage(true, true, false, false, false, false);
  if (!Expect(pending == ColumnJobStage::PendingLight, "pending light"))
  {
    return 1;
  }

  const ColumnJobStage meshing =
      DeriveColumnJobStage(true, false, false, true, false, false);
  if (!Expect(meshing == ColumnJobStage::Meshing, "meshing"))
  {
    return 1;
  }

  const ColumnJobStage gpu =
      DeriveColumnJobStage(true, false, false, false, true, false);
  if (!Expect(gpu == ColumnJobStage::GpuPending, "gpu pending"))
  {
    return 1;
  }

  const ColumnJobStage ready =
      DeriveColumnJobStage(true, false, false, false, false, true);
  if (!Expect(ready == ColumnJobStage::RenderReady, "render ready"))
  {
    return 1;
  }

  // Monotonic advance: meshing cannot regress to absent when chunk exists.
  const ColumnJobStage after_mesh =
      DeriveColumnJobStage(true, false, false, true, true, false);
  if (!Expect(after_mesh == ColumnJobStage::GpuPending,
              "meshing+gpu -> gpu pending"))
  {
    return 1;
  }

  std::cout << "column_job_graph_stage_test: PASS\n";
  return 0;
}
