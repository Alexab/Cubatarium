#include "Blocks/BlockDefinitionStorage.h"
#include "Core/Jobs/PipelineAdmission.h"
#include "Render/Mesh/MeshCaptureStore.h"
#include "World/Chunks/ChunkInputStamp.h"
#include "World/Core/BlockWorld.h"
#include <iostream>

int main()
{
  using cutum::ChunkInputStamp;
  using cutum::ChunkMeshSnapshot;
  using cutum::UMeshCaptureStore;
  using cutum::UPipelineAdmission;

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
  // Light-halo signatures of the 26 absent neighbors, in Capture's order.
  size_t signature_index = 0;
  for (int dy = -1; dy <= 1; ++dy)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int dx = -1; dx <= 1; ++dx)
      {
        if (dx == 0 && dy == 0 && dz == 0)
        {
          continue;
        }
        snap.lightHaloSignatures[signature_index++] =
            cutum::ChunkMeshLightHaloSignature(nullptr,
                                               glm::ivec3(dx, dy, dz));
      }
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
  // A23 D0: equal-rev hit returns the same committed bake (CaptureStore no-op
  // for remesh-without-Invalidate — root cause of sticky FullyDark).
  {
    auto hit = store.TryGet(world, coord, rev);
    if (!hit)
    {
      std::cerr << "FAIL: equal-rev TryGet must hit committed capture\n";
      return 1;
    }
    if (hit->sourceRevision != rev)
    {
      std::cerr << "FAIL: equal-rev hit must keep same sourceRevision\n";
      return 1;
    }
  }
  store.Invalidate(coord);
  if (store.TryGet(world, coord, rev))
  {
    std::cerr << "FAIL: Invalidate must miss equal-rev TryGet\n";
    return 1;
  }
  if (store.TakeOrRefresh(world, coord, rev, budget))
  {
    std::cerr << "FAIL: hard defer on miss when budget==0\n";
    return 1;
  }

  // Q7: CaptureAndStore refuses when snapshot credit is exhausted (same gate
  // as CaptureAndCommitOnMain on the mesh cache main path).
  auto &admission = UPipelineAdmission::Get();
  admission.SetSnapshotCap(0);
  if (store.CaptureAndStore(world, coord, rev).has_value())
  {
    std::cerr << "FAIL: CaptureAndStore must nullopt when snapshot cap=0\n";
    admission.SetSnapshotCap(96 * 1024 * 1024);
    return 1;
  }
  admission.SetSnapshotCap(96 * 1024 * 1024);
  if (admission.SnapshotPendingBytes() != 0)
  {
    std::cerr << "FAIL: rejected capture must not leave pending snapshot bytes\n";
    return 1;
  }

  cutum::UBlockWorld physical;
  physical.SetVerticalProfile({-61, 317, 57, -61, 9});
  physical.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  physical.GetChunkManager().EnsureChunk(coord);
  const auto catalog = std::make_shared<cutum::BlockDefinitionCatalog>();
  const auto captured = store.CaptureAndStore(physical, coord, rev);
  if (!captured || !captured->GeometryRenderInputs(catalog).valid ||
      captured->geometryProfile.MinY != -61 ||
      captured->worldGeometryIdentity !=
          physical.GetChunkManager().GetGeometryInputRevisionIdentity())
  {
    std::cerr
        << "FAIL: actual capture must preserve geometry render provenance\n";
    return 1;
  }
  const auto retained = captured->GeometryRenderInputs(catalog);
  const auto hit = store.TryGet(physical, coord, rev);
  if (!hit || !hit->GeometryRenderInputs(catalog).Matches(retained))
  {
    std::cerr
        << "FAIL: capture cache hit must retain exact geometry provenance\n";
    return 1;
  }
  auto legacy = *captured;
  legacy.worldGeometryIdentity.reset();
  if (legacy.GeometryRenderInputs(catalog).valid)
  {
    std::cerr << "FAIL: legacy missing world identity cannot claim a physical "
                 "binding\n";
    return 1;
  }
  physical.SetVerticalProfile({-64, 319});
  if (captured->GeometryRenderInputs(catalog).profile.MinY != -61)
  {
    std::cerr << "FAIL: retained capture must not stamp current profile over "
                 "old geometry\n";
    return 1;
  }

  std::cout << "capture_incremental_test: PASS\n";
  return 0;
}
