#ifndef IWORLDPERCEPTION_H
#define IWORLDPERCEPTION_H

#include "Activity/CreatureActivityTypes.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Movement/CreatureBodyProbe.h"
#include "Creatures/Movement/CreatureHabitatPolicy.h"
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace cutum
{

class IWorldPerception
{
public:
  virtual ~IWorldPerception() = default;
  virtual std::optional<ControlledCreatureInfo>
  QueryControlledCreatureInfo() const = 0;
  virtual std::vector<CreatureId> CreaturesInRadius(const glm::vec3 &center,
                                                    float radius) const = 0;
  virtual bool CanCreatureOccupyAt(CreatureHabitat habitat,
                                   const glm::vec3 &bodyOrigin,
                                   const glm::vec3 &sizeBlocks) const = 0;
  virtual bool HabitatAllows(HabitatContext ctx, CreatureHabitat habitat,
                             const glm::vec3 &bodyOrigin,
                             const glm::vec3 &sizeBlocks) const = 0;
  virtual BodyMoveResult ProbeBodyMove(CreatureId id, const glm::vec3 &origin,
                                       const glm::vec3 &delta,
                                       CreatureHabitat habitat,
                                       const glm::vec3 &sizeBlocks,
                                       HabitatContext targetContext) const = 0;
};

} // namespace cutum

#endif
