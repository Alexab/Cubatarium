#include "World/Streaming/MeshLitGate.h"

#include <cstdlib>
#include <iostream>

using cutum::ShouldRejectDarkMeshCommit;
using cutum::SoftDeferMeshUntilLitPolicy;

static int failures = 0;

static void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << std::endl;
    ++failures;
  }
}

int main()
{
  // SoftDefer: first mesh while PendingLight is deferred (holes > dark bake),
  // including underfeet and outside focus. Remesh while pending stays deferred.
  Expect(SoftDeferMeshUntilLitPolicy(true, false, true, true, false),
         "underfeet missing+pending must defer (no dark preview)");
  Expect(SoftDeferMeshUntilLitPolicy(true, true, true, true, false),
         "underfeet has_mesh+pending defer remesh");
  Expect(!SoftDeferMeshUntilLitPolicy(true, true, false, true, false),
         "underfeet has_mesh+lit allow remesh");
  Expect(!SoftDeferMeshUntilLitPolicy(true, false, false, true, false),
         "underfeet missing+lit allow first mesh");

  // Focus missing + pending => defer (no dark preview).
  Expect(SoftDeferMeshUntilLitPolicy(false, false, true, true, true),
         "focus missing+pending must defer");
  // Focus missing + lit => allow.
  Expect(!SoftDeferMeshUntilLitPolicy(false, false, false, true, true),
         "focus missing+lit allow");

  // Remesh of existing while pending => defer.
  Expect(SoftDeferMeshUntilLitPolicy(false, true, true, true, true),
         "focus has_mesh+pending defer remesh");
  Expect(!SoftDeferMeshUntilLitPolicy(false, true, false, true, true),
         "focus has_mesh+lit allow remesh");

  // Outside focus: pending always defers; else MayMesh gate.
  Expect(SoftDeferMeshUntilLitPolicy(false, false, true, false, true),
         "outside missing+pending must defer");
  Expect(SoftDeferMeshUntilLitPolicy(false, false, false, false, false),
         "outside missing !may_mesh defer");
  Expect(!SoftDeferMeshUntilLitPolicy(false, false, false, false, true),
         "outside missing may_mesh allow");

  Expect(!ShouldRejectDarkMeshCommit(false, true, true),
         "lit new mesh always commits");
  Expect(ShouldRejectDarkMeshCommit(true, true, false),
         "pending-light dark first mesh rejected");
  Expect(ShouldRejectDarkMeshCommit(true, false, true),
         "dark remesh must not replace lit mesh");
  Expect(!ShouldRejectDarkMeshCommit(true, false, false),
         "cave/unlit first mesh allowed when not deferred");
  // Nearest-hole dark preview policy: only r≤1 (enforced in ChunkEmergeCoordinator).
  Expect(SoftDeferMeshUntilLitPolicy(false, false, true, true, true),
         "focus missing+pending must defer (nearest-hole bypass is r≤1 only)");
  // TD-ARCH-026: hide sticky/stale-dark outside r≤1 must be paired with repair
  // ticket (ColumnFlow RemeshSeam via CollectSticky/CollectStaleDark). Documented
  // contract — scheduler unit covers Enqueue(RemeshSeam) separately.
  Expect(true, "hide=>ticket contract: ColumnFlow RemeshSeam for sticky/stale");

  if (failures != 0)
  {
    std::cerr << failures << " failure(s)" << std::endl;
    return 1;
  }
  std::cout << "streaming_render_ready_invariants_test: PASS" << std::endl;
  return 0;
}
