#include "World/Streaming/MeshLightStalePolicy.h"

#include <cstdlib>
#include <iostream>

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
  using cutum::IsMeshLightStale;
  using cutum::IsMeshLightStaleGpu;

  Expect(!IsMeshLightStale(2, 2), "equal revision not stale");
  Expect(IsMeshLightStale(1, 3), "meshed behind light");
  Expect(!IsMeshLightStale(0, 0), "zero revisions");

  Expect(!IsMeshLightStaleGpu(true, true, 2, 2),
         "A21-07: equal-rev dark face is not stale");
  Expect(!IsMeshLightStaleGpu(false, true, 1, 2), "not gpu resident");
  Expect(IsMeshLightStaleGpu(true, false, 1, 2), "GPU revision stale");
  Expect(!IsMeshLightStaleGpu(true, false, 2, 2), "GPU equal rev no dark face");

  if (failures != 0)
  {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
