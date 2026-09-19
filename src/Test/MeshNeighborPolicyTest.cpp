#include "Render/Mesh/MeshNeighborPolicy.h"
#include "World/Streaming/SeaSeamRemeshPolicy.h"
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
  // M09: SoftDefer-hidden solid -> Unlit (emit faces, no solid occlusion).
  if (cutum::ClassifyShellCell(true, static_cast<cutum::BlockId>(1),
                               /*neighbor_visually_drawable=*/false) !=
      cutum::NeighborLoadState::Unlit)
  {
    return Fail("loaded !drawable solid -> Unlit");
  }
  if (cutum::ClassifyShellCell(true, cutum::BLOCK_AIR,
                               /*neighbor_visually_drawable=*/false) !=
      cutum::NeighborLoadState::Air)
  {
    return Fail("loaded !drawable air -> Air");
  }
  if (cutum::ShouldSkipFaceForNeighbor(cutum::NeighborLoadState::Unlit))
  {
    return Fail("Unlit must not skip face");
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
  // R06: surface band keeps |cy-sea|<=2 inland; underwater widens to 4.
  const int sea_cy = 3;
  if (!cutum::ShouldRemeshSeaSeamOnFirstDrawable(sea_cy, sea_cy, false))
  {
    return Fail("surface sea_cy remesh");
  }
  if (cutum::ShouldRemeshSeaSeamOnFirstDrawable(sea_cy - 3, sea_cy, false))
  {
    return Fail("inland deep cy must not remesh without underwater gate");
  }
  if (!cutum::ShouldRemeshSeaSeamOnFirstDrawable(sea_cy - 3, sea_cy, true))
  {
    return Fail("underwater gate remeshes subsea cy");
  }
  if (cutum::SeaSeamRemeshMinY(62, 16, false) != 62 - 16)
  {
    return Fail("surface remesh min y");
  }
  if (cutum::SeaSeamRemeshMinY(62, 16, true) != 0)
  {
    return Fail("underwater remesh min y deeper (clamped >=0)");
  }
  std::cout << "mesh_neighbor_policy_test OK" << std::endl;
  return 0;
}
