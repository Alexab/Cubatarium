#include "Render/Mesh/MeshCaptureStore.h"
#include "Render/Mesh/MeshCaptureWorker.h"
#include "World/Core/BlockWorld.h"
#include <iostream>

int main()
{
  using cutum::ChunkMeshSnapshot;
  using cutum::DependencyStamp;
  using cutum::UMeshCaptureStore;
  using cutum::UMeshCaptureWorker;
  using cutum::WorkDomain;
  using cutum::WorkToken;

  UMeshCaptureStore store;
  const glm::ivec3 coord(0, 0, 0);
  const uint64_t rev = 7;
  ChunkMeshSnapshot snap;
  snap.coord = coord;
  snap.sourceRevision = rev;

  if (!store.TryCommit(coord, rev, store.WorldEpoch(), snap))
  {
    std::cerr << "FAIL: initial TryCommit\n";
    return 1;
  }
  const uint64_t stale_epoch = store.WorldEpoch();
  store.BumpWorldEpoch();
  if (store.TryCommit(coord, rev, stale_epoch, snap))
  {
    std::cerr << "FAIL: stale epoch must be rejected\n";
    return 1;
  }

  if (UMeshCaptureWorker::kWorkerCaptureEnabled)
  {
    UMeshCaptureWorker worker(1);
    cutum::UBlockWorld world;
    world.GetChunkManager().EnsureChunk(coord);
    WorkToken token;
    token.world_epoch = 1;
    token.coord = coord;
    token.domain = WorkDomain::MeshCapture;
    token.generation = 1;
    DependencyStamp deps;
    deps.content_revision = rev;
    snap = ChunkMeshSnapshot{};
    snap.coord = coord;
    worker.Enqueue(std::move(snap), token, deps);
    worker.CancelPending();
    const auto completed = worker.DrainCompleted(8);
    if (!completed.empty())
    {
      std::cerr << "FAIL: cancelled worker completion must be discarded\n";
      return 1;
    }
  }

  std::cout << "capture_token_validation_test: PASS\n";
  return 0;
}
