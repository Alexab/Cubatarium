#include "World/Streaming/MeshWorkAdmission.h"
#include "World/Streaming/SoftDeferEmptyPolicy.h"

#include <cstdlib>
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

} // namespace

int main()
{
  using cutum::ComputeMeshWorkAdmission;
  using cutum::IsMissFirstMeshClass;
  using cutum::IsSoftDeferEmptyPlaceholder;
  using cutum::MeshWorkAdmission;
  using cutum::MeshWorkAdmissionInput;
  using cutum::ShouldColdAsyncImmEscape;
  using cutum::ShouldEnqueueSoftDeferEmptyFirstMesh;

  Expect(IsMissFirstMeshClass(true, 0, 5), "cy0 tops class");
  Expect(IsMissFirstMeshClass(true, 3, 5), "Era20: cy3 tops class");
  Expect(IsMissFirstMeshClass(true, 5, 4), "Era20: mh4 tops class");
  Expect(!IsMissFirstMeshClass(true, 5, 5), "cy5 mh5 outside class");
  Expect(!IsMissFirstMeshClass(false, 0, 0), "no holes → no class");

  Expect(ShouldColdAsyncImmEscape(true, 0), "miss+async0 escape");
  Expect(ShouldColdAsyncImmEscape(true, 1), "miss+async1 escape");
  Expect(!ShouldColdAsyncImmEscape(true, 2), "async>=2 no escape");
  Expect(!ShouldColdAsyncImmEscape(false, 0), "no miss no escape");

  Expect(IsSoftDeferEmptyPlaceholder(true, false, false, false, false, true),
         "empty SoftDefer placeholder");
  Expect(!IsSoftDeferEmptyPlaceholder(true, true, false, false, false, true),
         "drawable not empty");
  Expect(ShouldEnqueueSoftDeferEmptyFirstMesh(true, 2, false),
         "stuck horiz>1 → FM");
  Expect(ShouldEnqueueSoftDeferEmptyFirstMesh(true, 0, true),
         "underfeet empty while miss → FM");
  Expect(!ShouldEnqueueSoftDeferEmptyFirstMesh(false, 5, true),
         "not placeholder → no FM");

  {
    MeshWorkAdmissionInput in;
    in.pending_gpu = 6;
    in.pending_gpu_queued = 0;
    in.pending_gpu_kicked = 6;
    in.visual_holes = true;
    in.moving = true;
    in.nearest_miss_cy = 3;
    in.nearest_miss_horiz = 4;
    in.ring_depth = 8;
    in.prev_mode = static_cast<uint8_t>(MeshWorkAdmission::Mode::HoleDrain);
    // P0 harness: predicate true; full remesh=0 wire lands in P1.
    Expect(IsMissFirstMeshClass(true, in.nearest_miss_cy, in.nearest_miss_horiz),
           "214034 witness is FirstMesh class");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "miss_first_mesh_class_test: OK\n";
  return EXIT_SUCCESS;
}
