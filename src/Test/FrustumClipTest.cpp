#include "Render/Camera/Frustum.h"

#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
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
  // Audit N02 clip-space counterexample: camera (100,50,100) look -Z,
  // AABB around (100,50,80) must be inside with row-based extraction.
  const glm::vec3 eye(100.f, 50.f, 100.f);
  const glm::mat4 view =
      glm::lookAt(eye, eye + glm::vec3(0.f, 0.f, -1.f), glm::vec3(0, 1, 0));
  const glm::mat4 proj =
      glm::perspective(glm::radians(60.f), 1.6f, 0.1f, 500.f);
  const glm::mat4 vp = proj * view;
  const cutum::Frustum fr = cutum::Frustum::FromViewProjection(vp);
  const glm::vec3 bmin(99.9f, 49.9f, 79.9f);
  const glm::vec3 bmax(100.1f, 50.1f, 80.1f);
  // Distance guard off — exercise plane math only.
  Expect(fr.IntersectsChunkAABB(bmin, bmax, eye, /*maxDistance=*/0.f),
         "N02: translated camera AABB inside clip is accepted");
  // Well outside lateral frustum (distance guard off).
  const glm::vec3 out_min(1000.f, 49.9f, 79.9f);
  const glm::vec3 out_max(1001.f, 50.1f, 80.1f);
  Expect(!fr.IntersectsChunkAABB(out_min, out_max, eye, 0.f),
         "N02: far lateral AABB rejected without distance bypass");
  // Audit S8 cull parity fixtures: near / far plane boundaries.
  const glm::vec3 near_min(99.9f, 49.9f, 99.7f);
  const glm::vec3 near_max(100.1f, 50.1f, 99.85f);
  Expect(fr.IntersectsChunkAABB(near_min, near_max, eye, 0.f),
         "S8: near-plane AABB inside clip accepted");
  const glm::vec3 far_min(99.9f, 49.9f, -399.f);
  const glm::vec3 far_max(100.1f, 50.1f, -398.f);
  Expect(fr.IntersectsChunkAABB(far_min, far_max, eye, 0.f),
         "S8: far-plane AABB inside clip accepted");
  const glm::vec3 beyond_far_min(99.9f, 49.9f, -600.f);
  const glm::vec3 beyond_far_max(100.1f, 50.1f, -599.f);
  Expect(!fr.IntersectsChunkAABB(beyond_far_min, beyond_far_max, eye, 0.f),
         "S8: beyond far clip rejected");
  // Real CPU cull parity: different AABB sizes / near-far placements (not
  // three identical Expects on the same box).
  const glm::vec3 tall_min(99.5f, 40.f, 70.f);
  const glm::vec3 tall_max(100.5f, 60.f, 90.f);
  Expect(fr.IntersectsChunkAABB(tall_min, tall_max, eye, 0.f),
         "S8: tall vertical AABB inside accepted");
  const glm::vec3 skim_min(99.9f, 49.9f, 99.05f);
  const glm::vec3 skim_max(100.1f, 50.1f, 99.2f);
  Expect(fr.IntersectsChunkAABB(skim_min, skim_max, eye, 0.f),
         "S8: near-skim AABB inside accepted");
  const glm::vec3 behind_min(99.9f, 49.9f, 101.f);
  const glm::vec3 behind_max(100.1f, 50.1f, 102.f);
  Expect(!fr.IntersectsChunkAABB(behind_min, behind_max, eye, 0.f),
         "S8: behind-camera AABB rejected");
  const glm::mat4 tight_proj =
      glm::perspective(glm::radians(40.f), 1.6f, 1.0f, 80.f);
  const cutum::Frustum tight =
      cutum::Frustum::FromViewProjection(tight_proj * view);
  Expect(!tight.IntersectsChunkAABB(far_min, far_max, eye, 0.f),
         "S8: far AABB rejected under tight far plane");
  Expect(tight.IntersectsChunkAABB(bmin, bmax, eye, 0.f),
         "S8: mid AABB still accepted under tight proj");
  if (gFails)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "frustum_clip_test: OK\n";
  return EXIT_SUCCESS;
}
