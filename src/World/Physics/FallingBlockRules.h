#ifndef FALLINGBLOCKRULES_H
#define FALLINGBLOCKRULES_H

#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
class UBlockDefinitionStorage;

class UFallingBlockRules
{
public:
  static bool CanFall(const UBlockRegistry &registry, const UBlockWorld &world,
                      glm::ivec3 blockPos);
  static bool TryApplyFall(const UBlockRegistry &registry, UBlockWorld &world,
                           glm::ivec3 blockPos);
  static bool CanFall(const UBlockDefinitionStorage &definitions,
                      const UBlockWorld &world, glm::ivec3 blockPos);
  static bool TryApplyFall(const UBlockDefinitionStorage &definitions,
                           UBlockWorld &world, glm::ivec3 blockPos);
};

} // namespace cutum

#endif // FALLINGBLOCKRULES_H
