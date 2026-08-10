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
    const auto d = DeriveColumnDesiredStage(false, true, false, true);
    Expect(d.stage == ColumnDesiredStage::RelightThenMesh,
           "pending_light beats stale remesh (Era19 exclusivity)");
  }
  {
    const auto d = DeriveColumnDesiredStage(false, true, false, false);
    Expect(d.stage == ColumnDesiredStage::RemeshSeam, "stale → RemeshSeam");
  }
  {
    const auto d = DeriveColumnDesiredStage(false, false, true, false);
    Expect(d.stage == ColumnDesiredStage::RelightThenMesh,
           "Era32: void → RelightThenMesh");
  }
  {
    const auto d = DeriveColumnDesiredStage(false, false, false, true);
    Expect(d.stage == ColumnDesiredStage::RelightThenMesh,
           "pending light → RelightThenMesh");
  }
  {
    const auto d = DeriveColumnDesiredStage(false, false, false, false,
                                            /*lit_pending=*/true);
    Expect(d.stage == ColumnDesiredStage::RemeshSeam,
           "LitPending (post-light sticky) → RemeshSeam");
  }
  {
    const auto d = DeriveColumnDesiredStage(false, false, true, false, false,
                                            /*unlit_published=*/true);
    Expect(d.stage == ColumnDesiredStage::RelightThenMesh,
           "void+UnlitPublished → RelightThenMesh");
  }
  {
    const auto d = DeriveColumnDesiredStage(false, false, false, false, false,
                                            false, /*dark_drawable=*/true);
    Expect(d.stage == ColumnDesiredStage::RelightThenMesh,
           "Era32: dark_drawable/VB → RelightThenMesh (not RemeshSeam)");
  }
  {
    const auto d = DeriveColumnDesiredStage(false, false, false, false, true,
                                            false, /*dark_drawable=*/true);
    Expect(d.stage == ColumnDesiredStage::RelightThenMesh,
           "Era32: dark_drawable beats lit_pending RemeshSeam");
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
