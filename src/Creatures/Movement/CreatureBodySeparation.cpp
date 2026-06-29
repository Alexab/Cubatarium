#include "Creatures/Movement/CreatureBodySeparation.h"
#include "Creatures/Movement/CreatureFootprint.h"
#include "Creatures/Movement/CreatureSeparationMath.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "World/Core/World.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"
#include <cmath>

namespace cutum
{

namespace
{

constexpr float kBasePushStep = 0.35f;
constexpr int kMaxPushRings = 6;

float ScaledPushStep(const glm::vec3 &size)
{
  const float footprint = std::min(size.x, size.z);
  return kBasePushStep *
         std::clamp(footprint / 0.6f, 0.5f, 1.5f);
}

bool CreatureBlockedAt(const UWorld &world, const glm::vec3 &body,
                       const glm::vec3 &size, CreatureId id)
{
  return world.CheckCreatureCollisionVolume(
      CollisionVolumeFromBody(body, size), id);
}

bool BlockedExceptGroundSupport(const UWorld &world, const glm::vec3 &body,
                                const glm::vec3 &size)
{
  const CollisionVolume vol = CollisionVolumeFromBody(body, size);
  if (!world.CheckBlockCollisionVolume(vol))
  {
    return false;
  }
  const FootprintSample footprint =
      SampleCreatureFootprint(world, body, size);
  return !CreatureHasGroundSupport(footprint);
}

bool TryCreatureOverlapSeparation(UWorld &world, UCreature &creature)
{
  const glm::vec3 size = creature.GetBounds().profile.restSizeBlocks;
  const CreatureId id = creature.GetId();
  const CollisionVolume selfVol = creature.GetCollisionVolume();

  glm::vec3 push(0.0f);
  world.ForEachCreature(
      [&](const UCreature &other)
      {
        if (other.GetId() == id)
        {
          return;
        }
        const CollisionVolume otherVol = other.GetCollisionVolume();
        const float penX =
            (selfVol.halfExtents.x + otherVol.halfExtents.x) -
            std::abs(selfVol.center.x - otherVol.center.x);
        const float penZ =
            (selfVol.halfExtents.z + otherVol.halfExtents.z) -
            std::abs(selfVol.center.z - otherVol.center.z);
        if (penX <= 0.0f || penZ <= 0.0f)
        {
          return;
        }
        if (const std::optional<glm::vec2> xzPush =
                ComputeOverlapSeparationPushXZ(
                    selfVol.center.x, selfVol.center.z, selfVol.halfExtents.x,
                    selfVol.halfExtents.z, otherVol.center.x, otherVol.center.z,
                    otherVol.halfExtents.x, otherVol.halfExtents.z))
        {
          push.x += xzPush->x;
          push.z += xzPush->y;
        }
      });
  if (glm::length(push) < 1e-5f)
  {
    return false;
  }
  const glm::vec3 trial = creature.GetBodyOrigin() + push;
  if (!BlockedExceptGroundSupport(world, trial, size))
  {
    creature.SetBodyOrigin(trial);
    return true;
  }
  return false;
}

bool TryHorizontalSeparation(UWorld &world, UCreature &creature)
{
  const glm::vec3 size = creature.GetBounds().profile.restSizeBlocks;
  const CreatureId id = creature.GetId();
  const glm::vec3 body = creature.GetBodyOrigin();

  static const glm::vec2 kPushDirs[] = {
      {1.0f, 0.0f},      {-1.0f, 0.0f},     {0.0f, 1.0f},
      {0.0f, -1.0f},     {0.707f, 0.707f},  {-0.707f, 0.707f},
      {0.707f, -0.707f}, {-0.707f, -0.707f}};
  for (int ring = 1; ring <= kMaxPushRings; ++ring)
  {
    const float dist = ScaledPushStep(size) * static_cast<float>(ring);
    for (const glm::vec2 &push : kPushDirs)
    {
      const glm::vec3 trial(body.x + push.x * dist, body.y,
                            body.z + push.y * dist);
      if (!CreatureBlockedAt(world, trial, size, id) &&
          !BlockedExceptGroundSupport(world, trial, size))
      {
        creature.SetBodyOrigin(trial);
        return true;
      }
    }
  }
  return false;
}

bool TrySnapToGroundColumn(UWorld &world, UCreature &creature)
{
  const glm::vec3 size = creature.GetBounds().profile.restSizeBlocks;
  const CreatureId id = creature.GetId();
  const glm::vec3 body = creature.GetBodyOrigin();
  const int gx = WorldCoordToBlockIndex(body.x);
  const int gz = WorldCoordToBlockIndex(body.z);
  if (const std::optional<float> feetY = world.QueryGroundFeetYColumn(gx, gz))
  {
    const glm::vec3 snapped(body.x, *feetY, body.z);
    if (!CreatureBlockedAt(world, snapped, size, id) &&
        !BlockedExceptGroundSupport(world, snapped, size))
    {
      creature.SetBodyOrigin(snapped);
      return true;
    }
  }
  return false;
}

} // namespace

bool CreatureOverlapsOthers(const UWorld &world, const UCreature &creature)
{
  const glm::vec3 size = creature.GetBounds().profile.restSizeBlocks;
  return CreatureBlockedAt(world, creature.GetBodyOrigin(), size,
                           creature.GetId());
}

bool SeparateFromBlocksAndCreatures(UWorld &world, UCreature &creature,
                                    int maxIterations)
{
  const glm::vec3 size = creature.GetBounds().profile.restSizeBlocks;
  const CreatureId id = creature.GetId();
  bool moved = false;
  for (int i = 0; i < maxIterations; ++i)
  {
    const glm::vec3 body = creature.GetBodyOrigin();
    const bool creatureOverlap = CreatureBlockedAt(world, body, size, id);
    const bool blockStuck = BlockedExceptGroundSupport(world, body, size);
    const bool blocked = creatureOverlap || blockStuck;
    if (!blocked)
    {
      break;
    }
    if (TryCreatureOverlapSeparation(world, creature) ||
        TrySnapToGroundColumn(world, creature) ||
        TryHorizontalSeparation(world, creature))
    {
      moved = true;
      continue;
    }
    break;
  }
  return moved;
}

} // namespace cutum
