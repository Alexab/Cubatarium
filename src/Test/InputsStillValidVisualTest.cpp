#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "World/Chunks/ChunkInputStamp.h"
#include "World/Core/BlockWorld.h"

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

bool DrawableAlwaysFalse(void *, glm::ivec3) { return false; }
bool DrawableAlwaysTrue(void *, glm::ivec3) { return true; }

} // namespace

/// Strategy A Phase1: SoftDefer / neighbor drawable flips must NOT invalidate
/// geom stamps (stops mesh_apply_stale_visual SoftDefer thrash class 175610).
int main()
{
  using cutum::ChunkInputStamp;
  using cutum::ChunkMeshSnapshot;
  using cutum::UBlockWorld;

  UBlockWorld world;
  const glm::ivec3 center(0, 0, 0);
  const glm::ivec3 neighbor(1, 0, 0);
  world.GetChunkManager().EnsureChunk(center);
  world.GetChunkManager().EnsureChunk(neighbor);
  auto *center_chunk = world.GetChunkManager().GetChunk(center);
  auto *neighbor_chunk = world.GetChunkManager().GetChunk(neighbor);
  Expect(center_chunk != nullptr, "center chunk resident");
  Expect(neighbor_chunk != nullptr, "neighbor chunk resident");

  // Capture with neighbor drawable=1 (shell may differ; stamp is geom-only).
  ChunkMeshSnapshot snap =
      ChunkMeshSnapshot::Capture(world, center, /*sourceRevision=*/1,
                                 DrawableAlwaysTrue, nullptr);
  Expect(snap.inputStampsValid, "capture stamps valid");
  Expect(snap.InputsStillValid(world, DrawableAlwaysTrue, nullptr),
         "still valid with same drawable fn");

  // Visual-only flip SoftDefer-hidden: no voxel/light edit.
  Expect(snap.InputsStillValid(world, DrawableAlwaysFalse, nullptr),
         "SoftDefer hide does not invalidate geom stamp");
  Expect(snap.InputsStillValid(world),
         "InputsStillValid without fn still true after visual flip");

  // Content edit still invalidates.
  neighbor_chunk->SetBlockLocal({0, 0, 0}, static_cast<cutum::BlockId>(1));
  Expect(!snap.InputsStillValid(world, DrawableAlwaysFalse, nullptr),
         "neighbor content edit invalidates geom stamp");

  if (gFails != 0)
  {
    std::cerr << gFails << " test(s) failed\n";
    return 1;
  }
  std::cout << "InputsStillValidVisualTest: PASS\n";
  return 0;
}
