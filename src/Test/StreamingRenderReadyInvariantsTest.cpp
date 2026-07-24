#include "World/Streaming/MeshLitGate.h"

#include <cstdlib>
#include <iostream>

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
  // Streaming SoftDefer: remesh of existing mesh while PendingLight is deferred
  // (including underfeet). Player dig/place no longer sets PendingLight.
  Expect(!SoftDeferMeshUntilLitPolicy(true, false, true, true, false),
         "underfeet missing+pending allow first mesh");
  Expect(SoftDeferMeshUntilLitPolicy(true, true, true, true, false),
         "underfeet has_mesh+pending defer remesh");
  Expect(!SoftDeferMeshUntilLitPolicy(true, true, false, true, false),
         "underfeet has_mesh+lit allow remesh");

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

  // Outside focus: MayMesh gate.
  Expect(SoftDeferMeshUntilLitPolicy(false, false, false, false, false),
         "outside missing !may_mesh defer");
  Expect(!SoftDeferMeshUntilLitPolicy(false, false, false, false, true),
         "outside missing may_mesh allow");

  if (failures != 0)
  {
    std::cerr << failures << " failure(s)" << std::endl;
    return 1;
  }
  std::cout << "streaming_render_ready_invariants_test: PASS" << std::endl;
  return 0;
}
