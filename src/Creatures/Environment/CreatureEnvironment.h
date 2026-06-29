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
  bool bodyBlocked{false};
};

void ApplyEnvironmentLocomotionFacts(const UWorld &world,
                                     const glm::vec3 &bodyOrigin,
                                     const glm::vec3 &sizeBlocks,
                                     CreatureLocomotionFacts &facts);

EnvironmentSample ProbeEnvironmentAt(const UWorld &world,
                                     const glm::vec3 &bodyOrigin,
                                     const glm::vec3 &sizeBlocks);

inline bool HabitatMatches(CreatureHabitat habitat,
                           const EnvironmentSample &env)
{
  switch (habitat)
  {
  case CreatureHabitat::Terrestrial:
    return env.onSolidGround && !env.inFluid;
  case CreatureHabitat::Aquatic:
    return env.inWater;
  case CreatureHabitat::Aerial:
    return env.inOpenAir && !env.inFluid;
  case CreatureHabitat::Amphibious:
    return (env.onSolidGround && !env.inFluid) || env.inWater;
  case CreatureHabitat::Lava:
    return env.inLava;
  }
  return false;
}

bool CanCreatureOccupyAt(const UWorld &world, CreatureHabitat habitat,
                         const glm::vec3 &bodyOrigin,
                         const glm::vec3 &sizeBlocks);

bool HabitatAllowsAt(const UWorld &world, CreatureHabitat habitat,
                     const glm::vec3 &bodyOrigin,
                     const glm::vec3 &sizeBlocks);

bool HabitatAllowsMovementAt(const UWorld &world, CreatureHabitat habitat,
                             const glm::vec3 &bodyOrigin,
                             const glm::vec3 &sizeBlocks);

bool HabitatAllowsAtForSpawn(const UWorld &world, CreatureHabitat habitat,
                             const glm::vec3 &bodyOrigin,
                             const glm::vec3 &sizeBlocks);

float ResolveViewerEyeHeight(const UWorld &world);

glm::ivec2 ResolveSpawnProbeColumn(const UWorld &world);

glm::vec3 SnapSpawnProbeToHabitat(const UWorld &world,
                                  const CreatureDefinition &def,
                                  const glm::vec3 &viewProbe);

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
