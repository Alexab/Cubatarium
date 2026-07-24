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

  return 0;
}
