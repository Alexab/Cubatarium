#include "World/View/ViewRayMath.h"

#include <cmath>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "view_ray_math_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  const glm::mat4 view =
      glm::lookAt(glm::vec3(10.0f, 20.0f, 30.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                  glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::mat4 proj = glm::ortho(-20.0f, 20.0f, -15.0f, 15.0f, 0.1f, 512.0f);
  const glm::ivec4 viewport(0, 0, 800, 600);

  glm::vec3 origin{};
  glm::vec3 dir{};
  Expect(cutum::ScreenPointToWorldRay(view, proj, viewport, 400.0f, 300.0f,
                                      origin, dir),
         "center unproject");
  Expect(std::abs(glm::length(dir) - 1.0f) < 1.0e-4f, "dir normalized");

  glm::vec3 corner_origin{};
  glm::vec3 corner_dir{};
  Expect(cutum::ScreenPointToWorldRay(view, proj, viewport, 0.0f, 0.0f,
                                      corner_origin, corner_dir),
         "corner unproject");
  // Ortho rays are parallel; origins must differ across the screen.
  Expect(glm::length(corner_origin - origin) > 0.5f,
         "corner origin differs from center");
  Expect(std::abs(glm::dot(dir, corner_dir) - 1.0f) < 1.0e-3f,
         "ortho dirs parallel");

  const float fovs[] = {45.0f, 70.0f, 100.0f};
  const float aspects[] = {4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f};
  for (float fov : fovs)
  {
    for (float aspect : aspects)
    {
      const glm::mat4 perspective =
          glm::perspective(glm::radians(fov), aspect, 0.1f, 2000.0f);
      const glm::mat4 rotated_view = glm::lookAt(
          glm::vec3(130.0f, -24.0f, 87.0f), glm::vec3(128.0f, -22.0f, 86.0f),
          glm::vec3(0.0f, 1.0f, 0.0f));
      cutum::ScreenRayTransform transform{};
      Expect(
          cutum::BuildScreenRayTransform(rotated_view, perspective, transform),
          "perspective inverse matrices");
      for (const glm::vec2 ndc :
           {glm::vec2(0.0f), glm::vec2(-1.0f, -1.0f), glm::vec2(0.83f, -0.62f)})
      {
        glm::vec3 sky_view{};
        cutum::CameraSpaceRay camera_ray{};
        Expect(cutum::ReconstructSkyViewRay(transform.inverse_projection, ndc.x,
                                            ndc.y, sky_view),
               "inverse-projection sky ray");
        Expect(cutum::ScreenNdcToCameraSpaceRay(transform, ndc.x, ndc.y,
                                                camera_ray),
               "reference camera ray");
        Expect(glm::length(sky_view - camera_ray.view_direction) < 1.0e-4f,
               "sky ray matches full camera projection");
      }
    }
  }
  const glm::mat4 ortho_projection =
      glm::ortho(-12.0f, 18.0f, -8.0f, 8.0f, 0.1f, 900.0f);
  cutum::ScreenRayTransform ortho_transform{};
  Expect(
      cutum::BuildScreenRayTransform(view, ortho_projection, ortho_transform),
      "orthographic inverse matrices");
  glm::vec3 ortho_sky_center{};
  glm::vec3 ortho_sky_corner{};
  cutum::CameraSpaceRay ortho_center_reference{};
  cutum::CameraSpaceRay ortho_corner_reference{};
  Expect(cutum::ReconstructSkyViewRay(ortho_transform.inverse_projection, 0.0f,
                                      0.0f, ortho_sky_center) &&
             cutum::ReconstructSkyViewRay(ortho_transform.inverse_projection,
                                          0.8f, -0.7f, ortho_sky_corner),
         "orthographic sky rays reconstruct");
  Expect(cutum::ScreenNdcToCameraSpaceRay(ortho_transform, 0.0f, 0.0f,
                                          ortho_center_reference) &&
             cutum::ScreenNdcToCameraSpaceRay(ortho_transform, 0.8f, -0.7f,
                                              ortho_corner_reference),
         "orthographic reference rays reconstruct");
  Expect(glm::length(ortho_sky_center - ortho_center_reference.view_direction) <
                 1.0e-4f &&
             glm::length(ortho_sky_corner -
                         ortho_corner_reference.view_direction) < 1.0e-4f,
         "orthographic sky rays match full camera projection");

  return 0;
}
