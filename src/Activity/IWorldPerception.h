#ifndef IWORLDPERCEPTION_H
#define IWORLDPERCEPTION_H

#include "Activity/CreatureActivityTypes.h"
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
};

} // namespace cutum

#endif
