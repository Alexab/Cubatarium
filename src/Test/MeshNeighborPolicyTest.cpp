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
  std::cout << "mesh_neighbor_policy_test OK" << std::endl;
  return 0;
}
