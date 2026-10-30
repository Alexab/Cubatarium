#ifndef CREATUREENVIRONMENT_H
#define CREATUREENVIRONMENT_H

#include <algorithm>
#include <optional>
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "World/Math/CollisionVolume.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace cutum
{

struct CreatureDefinition;
class UWorld;

/// Spawn/validate overlap volume: shrink by a small inset so skin-width
/// contacts (and tall mobs under low ceilings) do not count as overlap,
/// floored so the volume never collapses.
inline CollisionVolume InsetCollisionVolumeForSpawn(CollisionVolume vol)
{
  // Vertical only: lets tall mobs fit under a low ceiling. A horizontal
  // inset let wide bodies (cow 1.1) spawn poking into neighbouring columns.
  constexpr float kSpawnCollisionInset = 0.05f;
  vol.halfExtents.y = std::max(vol.halfExtents.y - kSpawnCollisionInset,
                               kSpawnCollisionInset);
  return vol;
}

struct EnvironmentSample
{
  bool inFluid{false};
  bool inWater{false};
  bool inLava{false};
  bool onSolidGround{false};
  bool inOpenAir{false};
  bool bodyBlocked{false};
};

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

void ApplyEnvironmentLocomotionFacts(const UWorld &world,
                                     const glm::vec3 &bodyOrigin,
                                     const glm::vec3 &sizeBlocks,
                                     CreatureLocomotionFacts &facts);

EnvironmentSample ProbeEnvironmentAt(const UWorld &world,
                                     const glm::vec3 &bodyOrigin,
                                     const glm::vec3 &sizeBlocks);

/// TTL-memoized variant for per-frame callers (vitals, movement habitat
/// gates, locomotion facts) — the volume scan is O(r^3) per block probe
/// (audit M2). Freshness bound: ~0.2s; spawn-time paths keep the plain
/// probe.
EnvironmentSample
ProbeEnvironmentAtMemoized(const UWorld &world, const glm::vec3 &bodyOrigin,
                           const glm::vec3 &sizeBlocks);

bool CanCreatureOccupyAt(const UWorld &world, CreatureHabitat habitat,
                         const glm::vec3 &bodyOrigin,
                         const glm::vec3 &sizeBlocks);

bool HabitatAllowsAt(const UWorld &world, CreatureHabitat habitat,
                     const glm::vec3 &bodyOrigin,
                     const glm::vec3 &sizeBlocks);

bool HabitatAllowsMovementAt(const UWorld &world, CreatureHabitat habitat,
                             const glm::vec3 &bodyOrigin,
                             const glm::vec3 &sizeBlocks,
                             float maxClimbDropBlocks = 1.25f);

bool HabitatAllowsAtForSpawn(const UWorld &world, CreatureHabitat habitat,
                             const glm::vec3 &bodyOrigin,
                             const glm::vec3 &sizeBlocks);

float ResolveViewerEyeHeight(const UWorld &world);

glm::ivec2 ResolveSpawnProbeColumn(const UWorld &world);

/// Ground feet Y for a terrestrial body: the highest support under any
/// footprint sample (center + edge/corner samples), so a body straddling a
/// one-block step stands on the higher block instead of sinking into it.
std::optional<float> ResolveTerrestrialFootprintFeetY(
    const UWorld &world, const glm::vec3 &origin, const glm::vec3 &sizeBlocks);

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

/// Nudge spawn origin on XZ when overlapping another creature AABB.
glm::vec3 TryDepenetrateSpawnOrigin(const UWorld &world,
                                    CreatureHabitat habitat,
                                    const glm::vec3 &bodyOrigin,
                                    const glm::vec3 &sizeBlocks,
                                    uint64_t skip_creature_id = 0);

/// Terrestrial mob move: horizontal slide with 1-block step up/down and feet snap.
glm::vec3 ResolveTerrestrialMobMovement(const UWorld &world,
                                        const glm::vec3 &bodyOrigin,
                                        const glm::vec3 &horizontalDelta,
                                        const glm::vec3 &sizeBlocks,
                                        uint64_t skip_creature_id,
                                        float max_step_up, float max_step_down);

} // namespace cutum

#endif
