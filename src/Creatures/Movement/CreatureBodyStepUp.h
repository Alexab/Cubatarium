#ifndef CREATUREBODYSTEPUP_H
#define CREATUREBODYSTEPUP_H

#include "Creatures/Locomotion/LocomotionTypes.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

using CreatureId = uint64_t;

class UWorld;

struct CreatureStepUpProbe
{
  bool valid{false};
  glm::vec3 landingBodyOrigin{0.0f};
  float distanceToLedge{0.0f};
};

bool CreatureStepUpAllowed(const UWorld &world, CreatureId id,
                           CreatureHabitat habitat, bool inFluid);

CreatureStepUpProbe ProbeCreatureStepUp(const UWorld &world, CreatureId id,
                                        const glm::vec3 &bodyOrigin,
                                        const glm::vec3 &horizDir,
                                        const glm::vec3 &sizeBlocks);

bool TryCreatureStepUp(const UWorld &world, CreatureId id,
                       glm::vec3 &bodyOrigin, const glm::vec3 &horizDelta,
                       const glm::vec3 &sizeBlocks);

bool TryCreatureEscapeStepUp(const UWorld &world, CreatureId id,
                             glm::vec3 &bodyOrigin,
                             const glm::vec3 &sizeBlocks,
                             CreatureHabitat habitat, bool inFluid);

} // namespace cutum

#endif
