#include "Render/Mesh/MeshCaptureStore.h"
#include "World/Core/BlockWorld.h"
#include <iostream>

int main()
{
  using cutum::ChunkMeshSnapshot;
  using cutum::UMeshCaptureStore;

  UMeshCaptureStore store;
  const glm::ivec3 coord(1, 2, 3);
  const uint64_t rev = 42;
  ChunkMeshSnapshot snap;
  snap.coord = coord;
  snap.sourceRevision = rev;
  store.Commit(coord, rev, snap);
  int budget = 0;
  cutum::UBlockWorld world;
  if (!store.TakeOrRefresh(world, coord, rev, budget))
  {
    std::cerr << "FAIL: store hit must work with budget==0\n";
    return 1;
  }
  store.Invalidate(coord);
  if (store.TakeOrRefresh(world, coord, rev, budget))
  {
    std::cerr << "FAIL: hard defer on miss when budget==0\n";
    return 1;
  }
  std::cout << "capture_incremental_test: PASS\n";
  return 0;
}
