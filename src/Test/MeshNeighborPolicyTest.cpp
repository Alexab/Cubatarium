#include "Render/Mesh/MeshNeighborPolicy.h"
#include <iostream>

static int Fail(const char *msg)
{
  std::cerr << "FAIL: " << msg << std::endl;
  return 1;
}

int main()
{
  if (!cutum::ShouldSkipFaceForNeighbor(cutum::NeighborLoadState::Unknown))
  {
    return Fail("Unknown must skip face");
  }
  if (cutum::ShouldSkipFaceForNeighbor(cutum::NeighborLoadState::Air))
  {
    return Fail("Air must not skip face");
  }
  if (cutum::ShouldSkipFaceForNeighbor(cutum::NeighborLoadState::Loaded))
  {
    return Fail("Loaded must not skip face");
  }
  if (cutum::ClassifyShellCell(false, cutum::BLOCK_AIR) !=
      cutum::NeighborLoadState::Unknown)
  {
    return Fail("unloaded chunk -> Unknown");
  }
  if (cutum::ClassifyShellCell(true, cutum::BLOCK_AIR) !=
      cutum::NeighborLoadState::Air)
  {
    return Fail("loaded air -> Air");
  }
  // Era39: SoftDefer-hidden / !drawable neighbor must not occlude (Air, not
  // Loaded solid / Unknown).
  if (cutum::ClassifyShellCell(true, static_cast<cutum::BlockId>(1),
                               /*neighbor_visually_drawable=*/false) !=
      cutum::NeighborLoadState::Air)
  {
    return Fail("loaded !drawable solid -> Air (Era39)");
  }
  if (cutum::ShellBlockForNeighborOcclusion(static_cast<cutum::BlockId>(1),
                                            false) != cutum::BLOCK_AIR)
  {
    return Fail("hidden neighbor occlusion block -> AIR");
  }
  if (cutum::ClassifyShellCell(true, static_cast<cutum::BlockId>(1), true) !=
      cutum::NeighborLoadState::Loaded)
  {
    return Fail("loaded drawable solid -> Loaded");
  }
  std::cout << "mesh_neighbor_policy_test OK" << std::endl;
  return 0;
}
