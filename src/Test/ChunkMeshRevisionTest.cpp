#include "Render/Mesh/ChunkMeshRevisionRegistry.h"
#include "Render/Mesh/MeshApplyPolicy.h"

#include <glm/glm.hpp>
#include <cstdlib>
#include <iostream>

static int failures = 0;

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << message << std::endl;
    ++failures;
  }
}

int main()
{
  using cutum::ClassifyMeshApplyRevision;
  using cutum::CpuReplaceFreeFirstWouldHole;
  using cutum::MeshApplyRevDecision;
  using cutum::ShouldKeepPriorGpuOnEmptyCpuReplace;
  using cutum::ShouldPublishCpuBatchesBeforeFreeGpu;
  using cutum::UChunkMeshRevisionRegistry;

  // --- Registry ---
  UChunkMeshRevisionRegistry revisions;
  const glm::ivec3 chunk_a{1, 0, 2};
  const glm::ivec3 chunk_b{4, 1, 5};

  Expect(revisions.Current(chunk_a) == 0, "unset chunk revision should be 0");
  const uint64_t a1 = revisions.Bump(chunk_a);
  Expect(a1 == 1, "first bump should be 1");
  Expect(revisions.Current(chunk_a) == 1, "current should match last bump");

  const uint64_t b1 = revisions.Bump(chunk_b);
  Expect(b1 == 1, "neighbor bump should be independent");
  Expect(revisions.Current(chunk_a) == 1,
         "bumping neighbor must not change chunk_a revision");

  revisions.Bump(chunk_a);
  Expect(revisions.Current(chunk_a) == 2, "second bump on chunk_a");
  Expect(revisions.Current(chunk_b) == 1, "chunk_b revision unchanged");

  revisions.Erase(chunk_a);
  Expect(revisions.Current(chunk_a) == 0, "erased chunk revision resets to 0");

  // --- Apply policy (manual_1957 thrash) ---
  // Active=R2, apply R1 → discard keep Active (no remesh).
  Expect(ClassifyMeshApplyRevision(true, /*active=*/2, /*result=*/1,
                                   /*current=*/2) ==
             MeshApplyRevDecision::DiscardOlderKeepActive,
         "older apply must not orphan Active R2");

  // Active=R1, Current=R2, apply R1 → remesh obsolete.
  Expect(ClassifyMeshApplyRevision(true, 1, 1, 2) ==
             MeshApplyRevDecision::RemeshObsoleteTracked,
         "obsolete tracked rev remeshes without bump");

  // No Active (CancelOutside) → drop, no Dirty.
  Expect(ClassifyMeshApplyRevision(false, 0, 1, 1) ==
             MeshApplyRevDecision::DropNoActive,
         "GPU pending without Active must DropNoActive");

  // Happy path.
  Expect(ClassifyMeshApplyRevision(true, 3, 3, 3) ==
             MeshApplyRevDecision::Commit,
         "matching Active+Current commits");

  // --- Era15 MeshResidency (TD-049) ---
  Expect(ShouldPublishCpuBatchesBeforeFreeGpu(),
         "CPU replace must publish batches before FreeChunk");
  Expect(CpuReplaceFreeFirstWouldHole(/*gpu_drawable=*/true,
                                      /*new_cpu=*/false),
         "GPU-only drawable free-first would hole");
  Expect(!CpuReplaceFreeFirstWouldHole(/*gpu_drawable=*/true,
                                       /*new_cpu=*/true),
         "CPU replacement ready: free-first still drawable via batches");
  Expect(!CpuReplaceFreeFirstWouldHole(/*gpu_drawable=*/false,
                                       /*new_cpu=*/false),
         "no GPU drawable: free-first not a residency hole");
  Expect(ShouldKeepPriorGpuOnEmptyCpuReplace(true, false),
         "Era20: keep prior GPU on empty SoftDefer replace");
  Expect(!ShouldKeepPriorGpuOnEmptyCpuReplace(true, true),
         "Era20: new CPU drawable → allow replace");

  if (failures != 0)
  {
    std::cerr << failures << " failure(s)" << std::endl;
    return 1;
  }
  std::cout << "chunk_mesh_revision_test: OK" << std::endl;
  return 0;
}
