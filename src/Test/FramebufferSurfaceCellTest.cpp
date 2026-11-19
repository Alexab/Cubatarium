#include "Render/Engine/FramebufferSurfaceCell.h"
#include <cstdlib>
#include <iostream>

using namespace cutum;
static void Expect(glm::ivec3 actual, glm::ivec3 expected)
{
  if (actual != expected)
  {
    std::cerr << "wrong framebuffer solid cell: " << actual.x << ',' << actual.y
              << ',' << actual.z << " expected " << expected.x << ','
              << expected.y << ',' << expected.z << '\n';
    std::exit(1);
  }
}
int main()
{
  // Native counterexample: a floor pixel centered at z48 belongs to chunk3,
  // even when its reconstructed z lies in [47.5,48), with negative X/Y too.
  Expect(FramebufferSurfaceCell({15.8f, -31.5f, 47.8f}, {0, -1, 0}),
         {16, -32, 48});
  Expect(FramebufferSurfaceCell({-16.2f, -31.5f, -.2f}, {0, -1, 0}),
         {-16, -32, 0});
  for (const glm::ivec3 center :
       {glm::ivec3(15, 31, 47), glm::ivec3(-16, -32, -48)})
    for (int axis = 0; axis < 3; ++axis)
      for (int sign : {-1, 1})
        for (float error : {-.0002f, 0.0f, .0002f})
        {
          glm::vec3 normal(0.0f);
          normal[axis] = float(sign);
          const auto point = glm::vec3(center) + normal * (.5f + error);
          Expect(FramebufferSurfaceCell(point, -normal), center);
        }
  std::cout << "framebuffer center-grid surface ownership PASS\n";
}
