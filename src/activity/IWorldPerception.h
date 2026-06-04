#ifndef IWORLDPERCEPTION_H
#define IWORLDPERCEPTION_H

#include <optional>
#include <vector>
#include <glm/glm.hpp>
#include "CreatureActivityTypes.h"

namespace cutum {

class IWorldPerception {
 public:
 virtual ~IWorldPerception() = default;
 virtual std::optional<ControlledCreatureInfo> GetControlledCreature() const = 0;
 virtual std::vector<CreatureId> CreaturesInRadius(const glm::vec3& center, float radius) const = 0;
};

} // namespace cutum

#endif
