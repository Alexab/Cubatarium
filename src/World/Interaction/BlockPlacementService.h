#ifndef BLOCK_PLACEMENT_SERVICE_H
#define BLOCK_PLACEMENT_SERVICE_H

#include <glm/glm.hpp>

namespace cutum
{

class UWorld;

/// Block/object placement from view ray (economy-gated for blocks).
class UBlockPlacementService
{
public:
  static bool AddObjectByView(UWorld &world, const glm::vec3 &position,
                              const glm::vec3 &front);
  static bool PlaceActiveObjectByView(UWorld &world, const glm::vec3 &position,
                                      const glm::vec3 &front);
};

} // namespace cutum

#endif
