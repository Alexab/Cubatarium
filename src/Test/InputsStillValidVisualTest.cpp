#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "World/Chunks/ChunkInputStamp.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/LightUtil.h"

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
  Expect(ChunkMeshSnapshot::ClassifyStaleInput(
             true, true, snap.inputStamps, world, snap.lightHaloSignatures) ==
             cutum::MeshApplyStaleInputReason::Geom,
         "content edit classifies as Geom");

  // Fresh capture after content edit, then light-only bump → Light.
  ChunkMeshSnapshot snap2 =
      ChunkMeshSnapshot::Capture(world, center, /*sourceRevision=*/2,
                                 DrawableAlwaysTrue, nullptr);
  Expect(snap2.InputsStillValid(world), "snap2 valid after content settle");
  neighbor_chunk->BumpLightFieldRevision();
  Expect(snap2.InputsStillValid(world),
         "unrelated neighbor light revision does not invalidate halo");
  Expect(ChunkMeshSnapshot::ClassifyStaleInput(
             true, true, snap2.inputStamps, world, snap2.lightHaloSignatures) ==
             cutum::MeshApplyStaleInputReason::Ok,
         "unrelated neighbor light revision classifies as Ok");
  auto &neighbor_light = neighbor_chunk->GetLightDataMutable();
  neighbor_light[cutum::UChunk::LocalIndex({0, 0, 0})] = cutum::PackLight(8, 0);
  Expect(!snap2.InputsStillValid(world), "halo light sample invalidates stamp");
  Expect(ChunkMeshSnapshot::ClassifyStaleInput(
             true, true, snap2.inputStamps, world, snap2.lightHaloSignatures) ==
             cutum::MeshApplyStaleInputReason::Light,
         "halo light sample classifies as Light");
  Expect(ChunkMeshSnapshot::ClassifyStaleInput(
             false, true, snap2.inputStamps, world, snap2.lightHaloSignatures) ==
             cutum::MeshApplyStaleInputReason::StampInvalid,
         "invalid flag classifies StampInvalid");
  Expect(ChunkMeshSnapshot::ClassifyStaleInput(
             true, false, snap2.inputStamps, world, snap2.lightHaloSignatures) ==
             cutum::MeshApplyStaleInputReason::Catalog,
         "catalog mismatch classifies Catalog");

  // S4 overlay: missing neighbor face activates overlay; drawable flip does not
  // thrash permanent stamps; overlay clears when neighbor present+drawable.
  {
    UBlockWorld world2;
    const glm::ivec3 c(2, 0, 2);
    world2.GetChunkManager().EnsureChunk(c);
    ChunkMeshSnapshot alone =
        ChunkMeshSnapshot::Capture(world2, c, 1, nullptr, nullptr);
    Expect(alone.boundaryOverlay.active ||
               alone.boundaryOverlay.missingNeighborFaces != 0,
           "unloaded neighbors activate overlay mask");
    const uint64_t ov0 = alone.boundaryOverlay.version;
    Expect(alone.InputsStillValid(world2, DrawableAlwaysFalse, nullptr),
           "overlay world still stamp-valid on drawable flip");
    // Raw shell preserved under overlay (not AIR-scrubbed by drawable).
    Expect(alone.GetNeighborLoadState(alone.ChunkOrigin() +
                                      glm::ivec3(-1, 0, 0)) ==
               cutum::NeighborLoadState::Unknown,
           "overlay face load-state is Unknown (prevent-emit closing walls)");
    world2.GetChunkManager().EnsureChunk(c + glm::ivec3(1, 0, 0));
    world2.GetChunkManager().EnsureChunk(c + glm::ivec3(-1, 0, 0));
    world2.GetChunkManager().EnsureChunk(c + glm::ivec3(0, 1, 0));
    world2.GetChunkManager().EnsureChunk(c + glm::ivec3(0, -1, 0));
    world2.GetChunkManager().EnsureChunk(c + glm::ivec3(0, 0, 1));
    world2.GetChunkManager().EnsureChunk(c + glm::ivec3(0, 0, -1));
    ChunkMeshSnapshot full =
        ChunkMeshSnapshot::Capture(world2, c, 2, DrawableAlwaysTrue, nullptr);
    Expect(!full.boundaryOverlay.active &&
               full.boundaryOverlay.missingNeighborFaces == 0,
           "all neighbors loaded clears overlay");
    Expect(full.boundaryOverlay.version != ov0 || !alone.boundaryOverlay.active,
           "overlay version advances or was inactive");
    ChunkMeshSnapshot undraw =
        ChunkMeshSnapshot::Capture(world2, c, 3, DrawableAlwaysFalse, nullptr);
    // Ownership SeamVisibility: loaded SoftDefer peers do NOT set overlay —
    // shell Unlit emits faces (sky-through fix SoT 161139).
    Expect(!undraw.boundaryOverlay.active &&
               undraw.boundaryOverlay.missingNeighborFaces == 0,
           "loaded SoftDefer peers leave overlay clear");
    Expect(undraw.GetNeighborLoadState(undraw.ChunkOrigin() +
                                       glm::ivec3(-1, 0, 0)) ==
               cutum::NeighborLoadState::Unlit ||
               undraw.GetNeighborLoadState(undraw.ChunkOrigin() +
                                           glm::ivec3(-1, 0, 0)) ==
                   cutum::NeighborLoadState::Air,
           "SoftDefer peer shell is Unlit/Air not Unknown");
    Expect(undraw.InputsStillValid(world2, DrawableAlwaysTrue, nullptr),
           "drawable flip keeps permanent stamp valid");
    (void)ov0;
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " test(s) failed\n";
    return 1;
  }
  std::cout << "InputsStillValidVisualTest: PASS\n";
  return 0;
}
