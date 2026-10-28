#include "Creatures/Visual/CreatureBonePaletteGpu.h"

#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_skinned_draw_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using namespace cutum;

  Expect(kCreatureBonePaletteMaxBones == 96,
         "bone palette max must be 96 (hermitcrab needs 67 joints)");
  Expect(kCreatureBonePaletteBindingPoint == 0,
         "bone palette binding point changed");
  Expect(ClampCreatureBonePaletteBoneCount(12) == 12,
         "small bone count clamp failed");
  Expect(ClampCreatureBonePaletteBoneCount(128) == 96,
         "clamp must cap at 96 bones");

  std::vector<glm::mat4> bones(96, glm::mat4(1.0f));
  bones[0] = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
  bones[95] = glm::translate(glm::mat4(1.0f), glm::vec3(9.0f, 0.0f, 0.0f));
  const auto palette = BuildCreatureBonePaletteData(bones);
  Expect(palette[0][3][0] == 1.0f && palette[0][3][1] == 2.0f &&
             palette[0][3][2] == 3.0f,
         "first bone transform mismatch");
  Expect(palette[95][3][0] == 9.0f, "96th bone transform mismatch");

  bones.resize(3);
  const auto shortPalette = BuildCreatureBonePaletteData(bones);
  Expect(shortPalette[3] == glm::mat4(1.0f),
         "unused palette slot must stay identity");

  std::cout << "creature_skinned_draw_test: OK" << std::endl;
  return 0;
}
