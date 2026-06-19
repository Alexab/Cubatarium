#include "Creatures/Environment/CreatureEnvironment.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <cmath>

namespace cutum
{
namespace
{

bool IsFluidBlock(const UBlockRegistry &registry, BlockId id)
{
  return id != BLOCK_AIR && !registry.BlocksMovement(id);
}

bool IsLavaBlock(const UBlockRegistry &registry, BlockId id)
{
  if (!IsFluidBlock(registry, id))
  {
    return false;
  }
  const std::string &name = registry.GetTypeNameById(id);
  return name.find("lava") != std::string::npos;
}

bool IsWaterBlock(const UBlockRegistry &registry, BlockId id)
{
  return IsFluidBlock(registry, id) && !IsLavaBlock(registry, id);
}

std::optional<float> FindAquaticFeetY(const UWorld &world, int worldX,
                                      int worldZ, bool wantLava)
{
  const UBlockRegistry &registry = world.GetBlockRegistry();
  const UBlockWorld &blockWorld = world.GetBlockWorld();
  int fluidMinY = -1;
  for (int y = 0; y <= 255; ++y)
  {
    const BlockId id =
        blockWorld.GetBlock(glm::ivec3(worldX, y, worldZ));
    const bool lava = IsLavaBlock(registry, id);
    const bool water = IsWaterBlock(registry, id);
    if (wantLava ? !lava : !water)
    {
      continue;
    }
    if (fluidMinY < 0)
    {
      fluidMinY = y;
    }
  }
  if (fluidMinY < 0)
  {
    return std::nullopt;
  }
  return static_cast<float>(fluidMinY);
}

bool HasAerialClearance(const UWorld &world, const CollisionVolume &vol,
                        float clearanceBlocks)
{
  const UBlockRegistry &registry = world.GetBlockRegistry();
  const glm::vec3 probeCenter =
      vol.center + glm::vec3(0.0f, vol.halfExtents.y + clearanceBlocks, 0.0f);
  const glm::ivec3 cell = WorldPosToBlock(probeCenter);
  return !registry.BlocksMovement(world.GetBlockWorld().GetBlock(cell));
}

} // namespace

EnvironmentSample ProbeEnvironmentAt(const UWorld &world,
                                     const glm::vec3 &bodyOrigin,
                                     const glm::vec3 &sizeBlocks)
{
  EnvironmentSample env;
  const CollisionVolume vol = CollisionVolumeFromBody(bodyOrigin, sizeBlocks);
  const UWorld::SampledFluidState fluid =
      world.SampleFluidPhysicsVolume(vol);
  env.inFluid = fluid.inFluid;
  if (fluid.inFluid)
  {
    const UBlockRegistry &registry = world.GetBlockRegistry();
    if (IsLavaBlock(registry, fluid.dominantFluid))
    {
      env.inLava = true;
    }
    else
    {
      env.inWater = true;
    }
  }
  env.onSolidGround =
      world.HasGroundSupportVolume(vol, BoundsFeetY(bodyOrigin));
  const bool bodyBlocked = world.CheckCollisionVolume(vol, 0);
  const bool clearance = HasAerialClearance(world, vol, 1.0f);
  env.inOpenAir =
      !env.inFluid && !env.onSolidGround && !bodyBlocked && clearance;
  return env;
}

bool HabitatMatches(CreatureHabitat habitat, const EnvironmentSample &env)
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
                         const glm::vec3 &sizeBlocks)
{
  const EnvironmentSample env =
      ProbeEnvironmentAt(world, bodyOrigin, sizeBlocks);
  if (!HabitatMatches(habitat, env))
  {
    return false;
  }
  const CollisionVolume vol = CollisionVolumeFromBody(bodyOrigin, sizeBlocks);
  return !world.CheckCollisionVolume(vol, 0);
}

bool CanSpawnCreatureAt(const UWorld &world, const CreatureDefinition &def,
                        const glm::vec3 &bodyOrigin)
{
  const glm::vec3 size = def.bounds.restSizeBlocks;
  const glm::vec3 adjusted =
      AdjustSpawnBodyOrigin(world, def, bodyOrigin);
  return CanCreatureOccupyAt(world, def.habitat, adjusted, size);
}

std::string HabitatRequirementLabel(CreatureHabitat habitat)
{
  switch (habitat)
  {
  case CreatureHabitat::Aquatic:
    return u8"Нужна вода";
  case CreatureHabitat::Aerial:
    return u8"Нужно открытое небо";
  case CreatureHabitat::Terrestrial:
    return u8"Нужна суша";
  case CreatureHabitat::Amphibious:
    return u8"Нужна суша или вода";
  case CreatureHabitat::Lava:
    return u8"Нужна лава";
  }
  return u8"Здесь нельзя заспавнить";
}

glm::vec3 AdjustSpawnBodyOrigin(const UWorld &world,
                                const CreatureDefinition &def,
                                const glm::vec3 &probeOrigin)
{
  if (def.habitat == CreatureHabitat::Aquatic ||
      def.habitat == CreatureHabitat::Amphibious)
  {
    const int gx = WorldCoordToBlockIndex(probeOrigin.x);
    const int gz = WorldCoordToBlockIndex(probeOrigin.z);
    if (const std::optional<float> feetY =
            FindAquaticFeetY(world, gx, gz, false))
    {
      const float halfHeight = def.bounds.restSizeBlocks.y * 0.5f;
      const float minFeet = *feetY;
      const float maxFeet =
          minFeet + static_cast<float>(255) - def.bounds.restSizeBlocks.y;
      const float feetYClamped =
          std::clamp(probeOrigin.y - halfHeight, minFeet, maxFeet);
      return glm::vec3(probeOrigin.x, feetYClamped, probeOrigin.z);
    }
    if (def.habitat == CreatureHabitat::Amphibious)
    {
      return probeOrigin;
    }
  }
  if (def.habitat == CreatureHabitat::Lava)
  {
    const int gx = WorldCoordToBlockIndex(probeOrigin.x);
    const int gz = WorldCoordToBlockIndex(probeOrigin.z);
    if (const std::optional<float> feetY =
            FindAquaticFeetY(world, gx, gz, true))
    {
      const float halfHeight = def.bounds.restSizeBlocks.y * 0.5f;
      const float feetYClamped = *feetY;
      return glm::vec3(probeOrigin.x,
                     std::clamp(probeOrigin.y - halfHeight, feetYClamped,
                                feetYClamped + 32.0f),
                     probeOrigin.z);
    }
    return probeOrigin;
  }
  return probeOrigin;
}

void ApplyEnvironmentLocomotionFacts(const UWorld &world,
                                     const glm::vec3 &bodyOrigin,
                                     const glm::vec3 &sizeBlocks,
                                     CreatureLocomotionFacts &facts)
{
  const EnvironmentSample env =
      ProbeEnvironmentAt(world, bodyOrigin, sizeBlocks);
  facts.inFluid = env.inWater || env.inLava;
  facts.onFluidBottom = env.inFluid && env.onSolidGround;
}

} // namespace cutum
