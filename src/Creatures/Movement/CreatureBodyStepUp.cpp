#include "Creatures/Movement/CreatureBodyStepUp.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "World/Core/World.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"

#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

constexpr float kMinStepRise = 0.45f;
constexpr float kMaxStepRise = 1.05f;
constexpr float kDirectionDeadZone = 0.25f;

glm::vec3 NormalizeHoriz(const glm::vec3 &horiz)
{
  const glm::vec3 flat(horiz.x, 0.0f, horiz.z);
  const float len = glm::length(flat);
  if (len < 1e-6f)
  {
    return glm::vec3(0.0f);
  }
  return flat / len;
}

float CreatureBodyRadius(const glm::vec3 &sizeBlocks)
{
  return std::max(sizeBlocks.x, sizeBlocks.z) * 0.5f;
}

float CreatureStepUpTriggerDistance(const glm::vec3 &sizeBlocks)
{
  return std::max(0.85f, CreatureBodyRadius(sizeBlocks) * 1.1f);
}

float CreatureStepUpForwardNudge(const glm::vec3 &sizeBlocks)
{
  return CreatureBodyRadius(sizeBlocks) * 0.35f;
}

bool FindCreatureSteppableLedge(const UWorld &world, const glm::vec3 &bodyOrigin,
                                const glm::vec3 &dir,
                                const glm::vec3 &sizeBlocks,
                                glm::ivec3 &outStepCell)
{
  const int dx = dir.x > kDirectionDeadZone ? 1
               : (dir.x < -kDirectionDeadZone ? -1 : 0);
  const int dz = dir.z > kDirectionDeadZone ? 1
               : (dir.z < -kDirectionDeadZone ? -1 : 0);
  if (dx == 0 && dz == 0)
  {
    return false;
  }
  const float feetY = BoundsFeetY(bodyOrigin);
  const int supportY = static_cast<int>(std::floor(feetY - 0.04f));
  const glm::ivec3 standCell(WorldCoordToBlockIndex(bodyOrigin.x), supportY,
                             WorldCoordToBlockIndex(bodyOrigin.z));
  const UBlockRegistry &registry = world.GetBlockRegistry();
  const UBlockWorld &blockWorld = world.GetBlockWorld();
  if (!registry.BlocksMovement(blockWorld.GetBlock(standCell)))
  {
    return false;
  }

  const auto tryLedge = [&](int riserY, int stepY) -> bool {
    const glm::ivec3 riserCell(standCell.x + dx, riserY, standCell.z + dz);
    if (!registry.BlocksMovement(blockWorld.GetBlock(riserCell)))
    {
      return false;
    }
    const glm::ivec3 stepCell(standCell.x + dx, stepY, standCell.z + dz);
    if (!world.IsValidStandCellForCreature(stepCell, sizeBlocks))
    {
      return false;
    }
    const float stepFeetY = BlockTopY(stepCell.y);
    const float rise = stepFeetY - feetY;
    if (rise < kMinStepRise || rise > kMaxStepRise)
    {
      return false;
    }
    const glm::vec3 landingBody(
        static_cast<float>(stepCell.x) + 0.5f, stepFeetY,
        static_cast<float>(stepCell.z) + 0.5f);
    const CollisionVolume landingVol =
        CollisionVolumeFromBody(landingBody, sizeBlocks);
    if (world.CheckBlockCollisionVolume(landingVol))
    {
      return false;
    }
    outStepCell = stepCell;
    return true;
  };

  if (tryLedge(supportY, supportY + 1))
  {
    return true;
  }
  return tryLedge(supportY + 1, supportY + 1);
}

float DistanceToCreatureStepRiser(const glm::vec3 &bodyOrigin,
                                  const glm::ivec3 &stepCell,
                                  const glm::vec3 &dir,
                                  const glm::vec3 &sizeBlocks)
{
  const glm::vec3 blockCenter(static_cast<float>(stepCell.x),
                              static_cast<float>(stepCell.y),
                              static_cast<float>(stepCell.z));
  const float radius = CreatureBodyRadius(sizeBlocks);
  const glm::vec3 facePoint =
      blockCenter - glm::vec3(dir.x * 0.5f, 0.0f, dir.z * 0.5f);
  const glm::vec3 bodyLead =
      bodyOrigin + glm::vec3(dir.x * radius, 0.0f, dir.z * radius);
  return glm::dot(facePoint - bodyLead, dir);
}

bool ApplyCreatureStepUpLanding(const UWorld &world, CreatureId id,
                                glm::vec3 &bodyOrigin, const glm::vec3 &dir,
                                const glm::ivec3 &stepCell,
                                const glm::vec3 &sizeBlocks)
{
  const float stepFeetY = BlockTopY(stepCell.y);
  const glm::vec3 nudge = dir * CreatureStepUpForwardNudge(sizeBlocks);
  const glm::vec3 landing(
      static_cast<float>(stepCell.x) + 0.5f + nudge.x, stepFeetY,
      static_cast<float>(stepCell.z) + 0.5f + nudge.z);
  const CollisionVolume vol = CollisionVolumeFromBody(landing, sizeBlocks);
  if (world.CheckBlockCollisionVolume(vol) ||
      world.CheckCreatureCollisionVolume(vol, id))
  {
    return false;
  }
  const glm::ivec3 feetCell = WorldPosToBlock(
      glm::vec3(bodyOrigin.x, BoundsFeetY(bodyOrigin) + 0.01f, bodyOrigin.z));
  const glm::ivec3 landingFeetCell(
      WorldCoordToBlockIndex(landing.x),
      static_cast<int>(std::floor(landing.y - 0.04f)),
      WorldCoordToBlockIndex(landing.z));
  if (feetCell.x == landingFeetCell.x && feetCell.z == landingFeetCell.z &&
      feetCell.y >= landingFeetCell.y)
  {
    return false;
  }
  bodyOrigin = landing;
  return true;
}

} // namespace

bool CreatureStepUpAllowed(const UWorld &world, CreatureId id,
                           CreatureHabitat habitat, bool inFluid)
{
  if (!world.IsStepUpEnabled() || inFluid)
  {
    return false;
  }
  if (habitat != CreatureHabitat::Terrestrial &&
      habitat != CreatureHabitat::Amphibious)
  {
    return false;
  }
  const UCreature *creature = world.GetCreature(id);
  if (!creature)
  {
    return false;
  }
  if (creature->GetMovementMode() != CreatureMovementMode::Walking)
  {
    return false;
  }
  switch (creature->GetLocomotionArchetype())
  {
  case LocomotionArchetype::TerrestrialBiped:
  case LocomotionArchetype::TerrestrialQuadruped:
    return true;
  default:
    return false;
  }
}

CreatureStepUpProbe ProbeCreatureStepUp(const UWorld &world, CreatureId id,
                                        const glm::vec3 &bodyOrigin,
                                        const glm::vec3 &horizDir,
                                        const glm::vec3 &sizeBlocks)
{
  CreatureStepUpProbe probe{};
  const glm::vec3 dir = NormalizeHoriz(horizDir);
  if (glm::length(dir) < 1e-6f)
  {
    return probe;
  }
  glm::ivec3 stepCell(0);
  if (!FindCreatureSteppableLedge(world, bodyOrigin, dir, sizeBlocks, stepCell))
  {
    return probe;
  }
  const float dist =
      DistanceToCreatureStepRiser(bodyOrigin, stepCell, dir, sizeBlocks);
  const float maxTrigger = CreatureStepUpTriggerDistance(sizeBlocks);
  if (dist < -0.02f || dist > maxTrigger)
  {
    return probe;
  }
  const float stepFeetY = BlockTopY(stepCell.y);
  probe.landingBodyOrigin =
      glm::vec3(static_cast<float>(stepCell.x) + 0.5f, stepFeetY,
                static_cast<float>(stepCell.z) + 0.5f);
  probe.distanceToLedge = dist;
  probe.valid = true;
  return probe;
}

bool TryCreatureStepUp(const UWorld &world, CreatureId id,
                       glm::vec3 &bodyOrigin, const glm::vec3 &horizDelta,
                       const glm::vec3 &sizeBlocks)
{
  const glm::vec3 dir = NormalizeHoriz(horizDelta);
  if (glm::length(dir) < 1e-6f)
  {
    return false;
  }
  const CreatureStepUpProbe probe =
      ProbeCreatureStepUp(world, id, bodyOrigin, dir, sizeBlocks);
  if (!probe.valid)
  {
    return false;
  }
  glm::ivec3 stepCell(0);
  if (!FindCreatureSteppableLedge(world, bodyOrigin, dir, sizeBlocks, stepCell))
  {
    return false;
  }
  return ApplyCreatureStepUpLanding(world, id, bodyOrigin, dir, stepCell,
                                    sizeBlocks);
}

bool TryCreatureEscapeStepUp(const UWorld &world, CreatureId id,
                             glm::vec3 &bodyOrigin,
                             const glm::vec3 &sizeBlocks,
                             CreatureHabitat habitat, bool inFluid)
{
  if (!CreatureStepUpAllowed(world, id, habitat, inFluid))
  {
    return false;
  }
  static const glm::vec3 kDirs[] = {
      glm::vec3(1.0f, 0.0f, 0.0f),   glm::vec3(-1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),   glm::vec3(0.0f, 0.0f, -1.0f),
      glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f)),
      glm::normalize(glm::vec3(-1.0f, 0.0f, 1.0f)),
      glm::normalize(glm::vec3(1.0f, 0.0f, -1.0f)),
      glm::normalize(glm::vec3(-1.0f, 0.0f, -1.0f))};
  int order[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  for (int i = 7; i > 0; --i)
  {
    const int j = std::rand() % (i + 1);
    const int tmp = order[i];
    order[i] = order[j];
    order[j] = tmp;
  }
  for (const int idx : order)
  {
    const glm::vec3 dir = kDirs[idx];
    glm::ivec3 stepCell(0);
    if (!FindCreatureSteppableLedge(world, bodyOrigin, dir, sizeBlocks, stepCell))
    {
      continue;
    }
    glm::vec3 trial = bodyOrigin;
    if (ApplyCreatureStepUpLanding(world, id, trial, dir, stepCell, sizeBlocks))
    {
      bodyOrigin = trial;
      return true;
    }
  }
  return false;
}

} // namespace cutum
