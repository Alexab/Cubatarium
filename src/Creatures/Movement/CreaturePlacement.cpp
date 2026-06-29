#include "Creatures/Movement/CreaturePlacement.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Environment/CreatureEnvironment.h"
#include "Render/Primitives/Cube.h"
#include "World/Core/World.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"

namespace cutum
{

namespace
{

constexpr int kSpawnSearchRadius = 5;
constexpr float kTerrestrialSpawnLift[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};

bool UsesTerrestrialSpawnLift(CreatureHabitat habitat)
{
  return habitat == CreatureHabitat::Terrestrial ||
         habitat == CreatureHabitat::Amphibious;
}

} // namespace

SpawnFailureReason ClassifySpawnFailureAt(const UWorld &world,
                                          const CreatureDefinition &def,
                                          const glm::vec3 &bodyOrigin,
                                          SpawnCollisionPolicy policy)
{
  const glm::vec3 size = def.bounds.restSizeBlocks;
  const glm::vec3 adjusted = AdjustSpawnBodyOrigin(world, def, bodyOrigin);
  if (!HabitatAllowsAtForSpawn(world, def.habitat, adjusted, size))
  {
    return SpawnFailureReason::Habitat;
  }
  const CollisionVolume vol = CollisionVolumeFromBody(adjusted, size);
  if (const UCreature *controlled = world.GetControlledCreature())
  {
    const CollisionVolume playerVol = controlled->GetCollisionVolume();
    if (UCube::CheckAabbCollision(vol.center, vol.halfExtents, playerVol.center,
                                  playerVol.halfExtents))
    {
      return SpawnFailureReason::Creature;
    }
  }
  if (policy == SpawnCollisionPolicy::Full)
  {
    const CreatureId skipCreature = world.GetControlledCreatureId();
    if (world.CheckCreatureCollisionVolume(vol, skipCreature))
    {
      return SpawnFailureReason::Creature;
    }
  }
  if (world.CheckBlockCollisionVolume(vol))
  {
    return SpawnFailureReason::Blocks;
  }
  return SpawnFailureReason::None;
}

PlacementResult FindSpawnOrigin(const UWorld &world,
                                const CreatureDefinition &def,
                                const glm::vec3 &viewProbe,
                                SpawnCollisionPolicy policy)
{
  PlacementResult result;
  const glm::vec3 snapped = SnapSpawnProbeToHabitat(world, def, viewProbe);
  const glm::vec3 baseOrigin = AdjustSpawnBodyOrigin(world, def, snapped);

  const auto tryCandidate = [&](const glm::vec3 &candidate) -> bool
  {
    if (ClassifySpawnFailureAt(world, def, candidate, policy) !=
        SpawnFailureReason::None)
    {
      return false;
    }
    result.bodyOrigin = AdjustSpawnBodyOrigin(world, def, candidate);
    result.failure = SpawnFailureReason::None;
    return true;
  };

  if (tryCandidate(baseOrigin))
  {
    return result;
  }

  const glm::ivec2 center(WorldCoordToBlockIndex(baseOrigin.x),
                          WorldCoordToBlockIndex(baseOrigin.z));
  for (const glm::ivec2 &offset : BuildSpawnSearchRing(kSpawnSearchRadius))
  {
    glm::vec3 candidate = baseOrigin;
    candidate.x = static_cast<float>(center.x + offset.x) + 0.5f;
    candidate.z = static_cast<float>(center.y + offset.y) + 0.5f;
    candidate = SnapSpawnProbeToHabitat(world, def, candidate);

    if (UsesTerrestrialSpawnLift(def.habitat))
    {
      for (const float lift : kTerrestrialSpawnLift)
      {
        glm::vec3 lifted = candidate;
        lifted.y += lift;
        if (tryCandidate(lifted))
        {
          return result;
        }
      }
    }
    else if (tryCandidate(candidate))
    {
      return result;
    }
  }

  result.failure = ClassifySpawnFailureAt(world, def, baseOrigin, policy);
  return result;
}

} // namespace cutum
