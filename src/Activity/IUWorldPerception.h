#ifndef IWORLDPERCEPTION_H
#define IWORLDPERCEPTION_H

#include "Activity/CreatureActivityTypes.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace cutum
{

class IUWorldPerception
{
public:
  virtual ~IUWorldPerception() = default;
  virtual std::optional<ControlledCreatureInfo>
  QueryControlledCreatureInfo() const = 0;
  virtual std::vector<CreatureId> CreaturesInRadius(const glm::vec3 &center,
                                                    float radius) const = 0;
  virtual std::vector<CreatureNeighborView>
  QueryCreatureNeighborsInRadius(const glm::vec3 &center, float radius,
                                 CreatureId skip_id) const = 0;
  virtual bool CreatureVolumeClearAt(const glm::vec3 &body_origin,
                                     const glm::vec3 &size_blocks,
                                     CreatureId skip_id) const = 0;
  virtual std::optional<glm::vec3>
  GetCreatureBodyOrigin(CreatureId id) const = 0;
  virtual bool CanCreatureOccupyAt(CreatureHabitat habitat,
                                   const glm::vec3 &bodyOrigin,
                                   const glm::vec3 &sizeBlocks) const = 0;
  virtual bool HabitatAllowsAt(CreatureHabitat habitat,
                               const glm::vec3 &bodyOrigin,
                               const glm::vec3 &sizeBlocks) const = 0;
  virtual bool HabitatAllowsMovementAt(CreatureHabitat habitat,
                                       const glm::vec3 &bodyOrigin,
                                       const glm::vec3 &sizeBlocks) const = 0;
  virtual bool IsWithinActivityRange(const glm::vec3 &body_origin) const = 0;
};

} // namespace cutum

#endif
