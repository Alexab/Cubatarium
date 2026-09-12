#include "Render/Mesh/MeshCaptureStore.h"
#include "World/Chunks/ChunkInputStamp.h"
#include "World/Core/BlockWorld.h"
#include <iostream>

int main()
{
  using cutum::ChunkInputStamp;
  using cutum::ChunkMeshSnapshot;
  using cutum::UMeshCaptureStore;

  UMeshCaptureStore store;
  const glm::ivec3 coord(1, 2, 3);
  const uint64_t rev = 42;
  ChunkMeshSnapshot snap;
  snap.coord = coord;
  snap.sourceRevision = rev;
  // Empty world: all stamps absent (incarnation 0) so TryGet/InputsStillValid hit.
  snap.inputStamps[0] = ChunkInputStamp::Capture(coord, nullptr);
  for (int axis = 0; axis < 3; ++axis)
  {
    for (int sign = -1; sign <= 1; sign += 2)
    {
      const int face = axis * 2 + (sign > 0 ? 1 : 0);
      glm::ivec3 neighbor = coord;
      neighbor[axis] += sign;
      snap.inputStamps[static_cast<size_t>(face + 1)] =
          ChunkInputStamp::Capture(neighbor, nullptr);
    }
  }
  snap.inputStampsValid = true;
  store.Commit(coord, rev, store.WorldEpoch(), snap);
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
