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
  // Actual native refusal: normal depth error exceeds the normal component
  // of the old ray bias. Retain both candidates until a drawn triangle proves
  // which section owns the hit; never snap the framebuffer point itself.
  const glm::vec3 native_point(-276.074f, -48.4991f, 42.277f);
  const glm::vec3 native_ray = glm::normalize(glm::vec3(-.9f, -.322949f, -.3f));
  Expect(FramebufferSurfaceCell(native_point, native_ray), {-276, -48, 42});
  const auto native_candidates =
      FramebufferSurfaceCandidateCells(native_point, native_ray);
  bool found_floor = false;
  for (size_t i = 0; i < native_candidates.count; ++i)
    found_floor |= native_candidates.cells[i] == glm::ivec3(-276, -49, 42);
  if (!found_floor || native_candidates.count != 2)
  {
    std::cerr << "native grazing-depth floor owner must remain a candidate\n";
    return 1;
  }
  // Retained unpacked native failure: AIR primary, CPU/MDI floor neighbor.
  // Either representation must be eligible after its actual draw probe, while
  // multiple draws/candidate sections can never certify a unique owner.
  for (const size_t owners : {size_t(0), size_t(1), size_t(2)})
  {
    int calls = 0;
    const auto recovered =
        RecoverFramebufferSurfaceSource(native_candidates,
                                        [&](glm::ivec3 coord)
                                        {
                                          ++calls;
                                          Expect(coord, {-18, -4, 2});
                                          return owners;
                                        });
    if (calls != 1 || recovered.tried != 2 || recovered.matches != owners ||
        recovered.Unique() != (owners == 1))
      return 1;
    if (recovered.Unique())
      Expect(recovered.coord, {-18, -4, 2});
  }
  const auto corner = FramebufferSurfaceCandidateCells(
      {15.5f, -32.5f, 47.5f}, glm::normalize(glm::vec3(1, -1, 1)));
  const auto ambiguous = RecoverFramebufferSurfaceSource(corner, [](glm::ivec3)
                                                         { return size_t(1); });
  if (ambiguous.Unique() || ambiguous.tried != 8 || ambiguous.matches != 7)
    return 1;
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
          const auto candidates = FramebufferSurfaceCandidateCells(
              glm::vec3(center) + normal * (.5f + .0009f),
              glm::normalize(-normal * .01f +
                             glm::vec3(axis == 0 ? 0.f : 1.f,
                                       axis == 1 ? 0.f : 1.f,
                                       axis == 2 ? 0.f : 1.f)));
          bool found = false;
          for (size_t i = 0; i < candidates.count; ++i)
            found |= candidates.cells[i] == center;
          if (!found || candidates.count > 8)
          {
            std::cerr << "grazing-depth candidate coverage on signed face\n";
            return 1;
          }
        }
  if (FramebufferSurfaceCandidateCells({.5f, -.5f, 15.5f}, {1, -1, 1}).count !=
          8 ||
      FramebufferSurfaceCandidateCells({0, 0, 0}, {0, -1, 0}).count != 1)
    return 1;
  std::cout << "framebuffer center-grid surface ownership PASS\n";
}
