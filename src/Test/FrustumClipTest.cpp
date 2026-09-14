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
  if (gFails)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "frustum_clip_test: OK\n";
  return EXIT_SUCCESS;
}
