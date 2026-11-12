#ifndef BLOCK_PLACEMENT_SERVICE_H
#define BLOCK_PLACEMENT_SERVICE_H

#include <glm/glm.hpp>
#include <string>

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
  /// Place `blockId` at the view target and consume one of the held item.
  static bool PlaceBlockFromHeldItem(UWorld &world, const glm::vec3 &position,
                                     const glm::vec3 &front,
                                     const std::string &blockId);
};

} // namespace cutum

#endif
