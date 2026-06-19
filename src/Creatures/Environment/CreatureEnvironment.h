#ifndef CREATUREENVIRONMENT_H
#define CREATUREENVIRONMENT_H

#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include <glm/glm.hpp>
#include <string>

namespace cutum
{

struct CollisionVolume;
struct CreatureDefinition;
class UWorld;

struct EnvironmentSample
{
  bool inFluid{false};
  bool inWater{false};
  bool inLava{false};
  bool onSolidGround{false};
  bool inOpenAir{false};
};

void ApplyEnvironmentLocomotionFacts(const UWorld &world,
                                     const glm::vec3 &bodyOrigin,
                                     const glm::vec3 &sizeBlocks,
                                     CreatureLocomotionFacts &facts);

EnvironmentSample ProbeEnvironmentAt(const UWorld &world,
                                     const glm::vec3 &bodyOrigin,
                                     const glm::vec3 &sizeBlocks);

bool HabitatMatches(CreatureHabitat habitat, const EnvironmentSample &env);

bool CanCreatureOccupyAt(const UWorld &world, CreatureHabitat habitat,
                         const glm::vec3 &bodyOrigin,
                         const glm::vec3 &sizeBlocks);

bool CanSpawnCreatureAt(const UWorld &world, const CreatureDefinition &def,
                        const glm::vec3 &bodyOrigin);

std::string HabitatRequirementLabel(CreatureHabitat habitat);

std::string GetCreatureSpawnBlockedHint(const UWorld &world,
                                        const CreatureDefinition &def,
                                        const glm::vec3 &bodyOrigin);

glm::vec3 AdjustSpawnBodyOrigin(const UWorld &world,
                                const CreatureDefinition &def,
                                const glm::vec3 &probeOrigin);

} // namespace cutum

#endif
