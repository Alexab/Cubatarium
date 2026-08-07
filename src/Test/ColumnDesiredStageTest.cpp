#include "World/Streaming/ColumnDesiredStage.h"

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
  using cutum::ColumnDesiredStage;
  using cutum::DeriveColumnDesiredStage;

  {
    const auto d = DeriveColumnDesiredStage(true, true, true, true);
    Expect(d.stage == ColumnDesiredStage::FirstMesh, "miss beats stale/void");
    Expect(d.enqueue_without_wall_gate, "FirstMesh no wall gate");
  }
  {
    const auto d = DeriveColumnDesiredStage(false, true, false, false);
    Expect(d.stage == ColumnDesiredStage::RemeshSeam, "stale → RemeshSeam");
  }
  {
    const auto d = DeriveColumnDesiredStage(false, false, true, false);
    Expect(d.stage == ColumnDesiredStage::RelightOnly, "void → RelightOnly");
  }
  {
    const auto d = DeriveColumnDesiredStage(false, false, false, true);
    Expect(d.stage == ColumnDesiredStage::RelightThenMesh,
           "pending light → RelightThenMesh");
  }
  {
    const auto d = DeriveColumnDesiredStage(false, false, false, false);
    Expect(d.stage == ColumnDesiredStage::None, "idle SoT → None");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "column_desired_stage_test: OK\n";
  return EXIT_SUCCESS;
}
