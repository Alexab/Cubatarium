#include "Render/Mesh/MeshNeighborPolicy.h"
#include "World/Streaming/SeaSeamRemeshPolicy.h"
#include "World/Streaming/FogPullInPolicy.h"
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
  // SoT 090834: air-near-fluid must NOT open subsea_band.
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
  {
    using cutum::SeaSeamEyeContext;
    if (cutum::ClassifySeaSeamEyeContext(70.0f, 62.0f, true) !=
        SeaSeamEyeContext::AirNearFluid)
    {
      return Fail("eye above sea + near fluid -> AirNearFluid");
    }
    if (cutum::ClassifySeaSeamEyeContext(50.0f, 62.0f, false) !=
        SeaSeamEyeContext::Underwater)
    {
      return Fail("eye below sea+2 -> Underwater");
    }
    if (cutum::ShouldRemeshSeaSeamOnFirstDrawable(
            sea_cy - 3, sea_cy, SeaSeamEyeContext::AirNearFluid))
    {
      return Fail("air-near-fluid must not remesh subsea cy");
    }
    if (!cutum::ShouldRemeshSeaSeamOnFirstDrawable(
            sea_cy, sea_cy, SeaSeamEyeContext::AirNearFluid))
    {
      return Fail("air-near-fluid still remeshes surface cy");
    }
    int min_y = 0;
    int max_y = 0;
    cutum::SeaSeamRemeshYRangeForPublisher(
        62, 16, 256, /*publisher_cy=*/1, SeaSeamEyeContext::AirNearFluid, min_y,
        max_y);
    if (min_y != 62 - 16)
    {
      return Fail("air-near-fluid Y range is surface (not publisher_cy)");
    }
  }
  if (cutum::SeaSeamRemeshMinYSurface(62, 16) != 62 - 16)
  {
    return Fail("surface remesh min y");
  }
  // E0/E2: underwater Y is publisher_cy only (not sea-4*CHUNK flood).
  {
    int min_y = 0;
    int max_y = 0;
    const int publisher_cy = 1;
    cutum::SeaSeamRemeshYRangeForPublisher(62, 16, 256, publisher_cy, true,
                                           min_y, max_y);
    if (min_y != 16 || max_y != 31)
    {
      return Fail("underwater remesh Y = publisher_cy only");
    }
    const int per_col = cutum::SeaSeamRemeshChunksPerPeerColumn(
        62, 16, 256, publisher_cy, true);
    if (per_col != 1)
    {
      return Fail("underwater dirty chunks/column must be 1 (same cy)");
    }
    if (per_col * 4 >= 9 * 5)
    {
      return Fail("underwater remesh still flood-class");
    }
  }
  if (cutum::SeaSeamPeerFaceTowardPublisher(1, 0) != 0 ||
      cutum::SeaSeamPeerFaceTowardPublisher(-1, 0) != 1 ||
      cutum::SeaSeamPeerFaceTowardPublisher(0, 1) != 4 ||
      cutum::SeaSeamPeerFaceTowardPublisher(0, -1) != 5)
  {
    return Fail("peer face toward publisher mapping");
  }
  if (cutum::PeerNeedsSeaSeamRemesh(false, 0xff, 0))
  {
    return Fail("inactive overlay must not remesh");
  }
  if (!cutum::PeerNeedsSeaSeamRemesh(true, 1u << 0, 0))
  {
    return Fail("active overlay face bit must remesh");
  }
  if (cutum::PeerNeedsSeaSeamRemesh(true, 1u << 1, 0))
  {
    return Fail("wrong face bit must not remesh");
  }
  if (!cutum::ShouldLatchFogHoleDebtUnfinishedOrVb(1, 0) ||
      !cutum::ShouldLatchFogHoleDebtUnfinishedOrVb(0, 1) ||
      cutum::ShouldLatchFogHoleDebtUnfinishedOrVb(0, 0))
  {
    return Fail("fog unfinished/VB latch");
  }
  if (!cutum::ShouldLatchFogHoleDebtNow(1, 1, 0, 0) ||
      !cutum::ShouldLatchFogHoleDebtNow(0, 9, 3, 0))
  {
    return Fail("fog hole_debt_now near-miss or unfinished");
  }
  if (!cutum::ShouldClearFogHoleDebtLatch(0, 0, 0, 0) ||
      cutum::ShouldClearFogHoleDebtLatch(0, 0, 2, 0))
  {
    return Fail("fog clear latch only when unfinished/VB gone");
  }
  {
    int min_y = 0;
    int max_y = 0;
    cutum::SeaSeamRemeshYRangeForPublisher(62, 16, 256, sea_cy, false, min_y,
                                           max_y);
    if (min_y != 62 - 16)
    {
      return Fail("inland surface min y KEEP");
    }
  }
  std::cout << "mesh_neighbor_policy_test OK" << std::endl;
  return 0;
}
