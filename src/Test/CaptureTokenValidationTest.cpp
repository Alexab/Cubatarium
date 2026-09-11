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

  // Exercise the production capture/cache path, not just stamp equality.
  // Keep sourceRevision fixed: input revisions must independently reject reuse.
  cutum::UBlockWorld capturedWorld;
  capturedWorld.GetChunkManager().EnsureChunk(coord);
  store.CaptureAndStore(capturedWorld, coord, rev);
  if (!store.TryGet(capturedWorld, coord, rev))
  {
    std::cerr << "FAIL: unchanged capture must be reusable\n";
    return 1;
  }
  const glm::ivec3 neighbor(1, 0, 0);
  capturedWorld.GetChunkManager().EnsureChunk(neighbor);
  if (store.TryGet(capturedWorld, coord, rev))
  {
    std::cerr << "FAIL: absent neighbor becoming resident must invalidate\n";
    return 1;
  }
  store.CaptureAndStore(capturedWorld, coord, rev);
  auto *neighborChunk = capturedWorld.GetChunkManager().GetChunk(neighbor);
  neighborChunk->SetLightLocal({0, 0, 0}, 7, 2);
  if (store.TryGet(capturedWorld, coord, rev))
  {
    std::cerr << "FAIL: changed halo light must invalidate cached capture\n";
    return 1;
  }
  store.CaptureAndStore(capturedWorld, coord, rev);
  neighborChunk->ResetForReuse(neighbor);
  if (store.TryGet(capturedWorld, coord, rev))
  {
    std::cerr << "FAIL: recycled neighbor must invalidate cached capture\n";
    return 1;
  }

  store.CaptureAndStore(capturedWorld, coord, rev);
  capturedWorld.GetChunkManager().GetChunk(coord)->SetBlockLocal(
      {1, 1, 1}, static_cast<cutum::BlockId>(1));
  auto refreshed = store.RefreshIncrementalShell(capturedWorld, coord, rev, 1);
  if (!refreshed || !refreshed->InputsStillValid(capturedWorld) ||
      refreshed->GetBlockLocal({1, 1, 1}) != static_cast<cutum::BlockId>(1))
  {
    std::cerr << "FAIL: shell refresh must recapture stale interior inputs\n";
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
