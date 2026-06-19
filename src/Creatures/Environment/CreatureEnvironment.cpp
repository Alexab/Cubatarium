#include "Creatures/Environment/CreatureEnvironment.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Render/Camera/Camera.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"
#include "World/Raycast/BlockRaycast.h"
#include <algorithm>
#include <cmath>

namespace cutum
{
namespace
{

constexpr float kDefaultViewerEyeHeight = 1.62f;
constexpr float kSpawnAheadBlocks = 3.0f;
constexpr int kFluidColumnSearchRadius = 2;

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

struct FluidColumn
{
  int minY{-1};
  int maxY{-1};
};

std::optional<FluidColumn> ScanFluidColumn(const UWorld &world, int worldX,
                                           int worldZ, bool wantLava)
{
  const UBlockRegistry &registry = world.GetBlockRegistry();
  const UBlockWorld &blockWorld = world.GetBlockWorld();
  FluidColumn column;
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
    if (column.minY < 0)
    {
      column.minY = y;
    }
    column.maxY = y;
  }
  if (column.minY < 0)
  {
    return std::nullopt;
  }
  return column;
}

std::optional<float> FindAquaticFeetY(const UWorld &world, int worldX,
                                      int worldZ, bool wantLava)
{
  if (const std::optional<FluidColumn> column =
          ScanFluidColumn(world, worldX, worldZ, wantLava))
  {
    return static_cast<float>(column->minY);
  }
  return std::nullopt;
}

std::optional<glm::ivec2> FindNearestFluidColumn(const UWorld &world, int gx,
                                                 int gz, bool wantLava,
                                                 int radius)
{
  for (int ring = 0; ring <= radius; ++ring)
  {
    for (int dx = -ring; dx <= ring; ++dx)
    {
      for (int dz = -ring; dz <= ring; ++dz)
      {
        if (ring > 0 && std::abs(dx) != ring && std::abs(dz) != ring)
        {
          continue;
        }
        const int cx = gx + dx;
        const int cz = gz + dz;
        if (FindAquaticFeetY(world, cx, cz, wantLava))
        {
          return glm::ivec2(cx, cz);
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<float> FindAquaticSpawnFeetY(const UWorld &world, int worldX,
                                           int worldZ, bool wantLava,
                                           float bodyHeight)
{
  const std::optional<FluidColumn> column =
      ScanFluidColumn(world, worldX, worldZ, wantLava);
  if (!column)
  {
    return std::nullopt;
  }
  const float depth =
      static_cast<float>(column->maxY - column->minY + 1);
  if (depth + 1e-3f < bodyHeight * 0.35f)
  {
    return std::nullopt;
  }
  const float minFeet = static_cast<float>(column->minY);
  const float maxFeet =
      static_cast<float>(column->maxY) - bodyHeight + 0.5f;
  return std::clamp(minFeet, minFeet, std::max(minFeet, maxFeet));
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

EnvironmentSample ProbeEnvironmentAtEx(const UWorld &world,
                                       const glm::vec3 &bodyOrigin,
                                       const glm::vec3 &sizeBlocks,
                                       float aerialClearance)
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
  env.bodyBlocked = world.CheckCollisionVolume(vol, 0);
  const bool clearance = HasAerialClearance(world, vol, aerialClearance);
  env.inOpenAir = !env.inFluid && !env.onSolidGround && !env.bodyBlocked &&
                  clearance;
  return env;
}

} // namespace

EnvironmentSample ProbeEnvironmentAt(const UWorld &world,
                                     const glm::vec3 &bodyOrigin,
                                     const glm::vec3 &sizeBlocks)
{
  return ProbeEnvironmentAtEx(world, bodyOrigin, sizeBlocks, 1.0f);
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

float ResolveViewerEyeHeight(const UWorld &world)
{
  if (const UCreature *player = world.GetPlayerCreature())
  {
    return player->GetEyePosition().y - player->GetBodyOrigin().y;
  }
  return kDefaultViewerEyeHeight;
}

glm::ivec2 ResolveSpawnProbeColumn(const UWorld &world)
{
  if (auto camera = world.GetCurrentUserCamera())
  {
    const glm::vec3 origin = camera->GetPosition();
    const glm::vec3 front = camera->GetFront();
    const auto hit = RaycastSolidBlocks(world.GetBlockWorld(),
                                        world.GetBlockRegistry(), origin, front,
                                        128.0f);
    if (hit)
    {
      return glm::ivec2(hit->blockPos.x, hit->blockPos.z);
    }
    glm::vec3 forward = front;
    forward.y = 0.0f;
    if (glm::length(forward) > 0.01f)
    {
      const glm::vec3 ahead =
          origin + glm::normalize(forward) * kSpawnAheadBlocks;
      return glm::ivec2(WorldCoordToBlockIndex(ahead.x),
                         WorldCoordToBlockIndex(ahead.z));
    }
  }
  if (const UCreature *player = world.GetPlayerCreature())
  {
    const glm::vec3 origin = player->GetBodyOrigin() + glm::vec3(3.0f, 0.0f, 0.0f);
    return glm::ivec2(WorldCoordToBlockIndex(origin.x),
                      WorldCoordToBlockIndex(origin.z));
  }
  const glm::vec3 spawn = world.GetSpawnPoint();
  return glm::ivec2(WorldCoordToBlockIndex(spawn.x),
                    WorldCoordToBlockIndex(spawn.z));
}

glm::vec3 SnapSpawnProbeToHabitat(const UWorld &world,
                                  const CreatureDefinition &def,
                                  const glm::vec3 &viewProbe)
{
  const glm::ivec2 column = ResolveSpawnProbeColumn(world);
  const int gx = column.x;
  const int gz = column.y;
  const float bodyHeight = def.bounds.restSizeBlocks.y;

  if (def.habitat == CreatureHabitat::Terrestrial)
  {
    const int probeGx = WorldCoordToBlockIndex(viewProbe.x);
    const int probeGz = WorldCoordToBlockIndex(viewProbe.z);
    if (const std::optional<float> feetY =
            world.QueryGroundFeetYColumn(probeGx, probeGz))
    {
      return glm::vec3(viewProbe.x, *feetY, viewProbe.z);
    }
    return viewProbe;
  }

  if (def.habitat == CreatureHabitat::Aquatic)
  {
    glm::ivec2 fluidCol(gx, gz);
    if (!FindAquaticFeetY(world, gx, gz, false))
    {
      if (const std::optional<glm::ivec2> nearCol =
              FindNearestFluidColumn(world, gx, gz, false,
                                     kFluidColumnSearchRadius))
      {
        fluidCol = *nearCol;
      }
    }
    if (const std::optional<float> feetY = FindAquaticSpawnFeetY(
            world, fluidCol.x, fluidCol.y, false, bodyHeight))
    {
      return glm::vec3(viewProbe.x, *feetY, viewProbe.z);
    }
    if (const std::optional<float> feetY =
            FindAquaticFeetY(world, fluidCol.x, fluidCol.y, false))
    {
      return glm::vec3(viewProbe.x, *feetY, viewProbe.z);
    }
    return viewProbe;
  }

  if (def.habitat == CreatureHabitat::Lava)
  {
    glm::ivec2 fluidCol(gx, gz);
    if (!FindAquaticFeetY(world, gx, gz, true))
    {
      if (const std::optional<glm::ivec2> nearCol =
              FindNearestFluidColumn(world, gx, gz, true,
                                     kFluidColumnSearchRadius))
      {
        fluidCol = *nearCol;
      }
    }
    if (const std::optional<float> feetY = FindAquaticSpawnFeetY(
            world, fluidCol.x, fluidCol.y, true, bodyHeight))
    {
      return glm::vec3(viewProbe.x, *feetY, viewProbe.z);
    }
    return viewProbe;
  }

  if (def.habitat == CreatureHabitat::Amphibious)
  {
    glm::ivec2 fluidCol(gx, gz);
    bool hasWater = static_cast<bool>(FindAquaticFeetY(world, gx, gz, false));
    if (!hasWater)
    {
      if (const std::optional<glm::ivec2> nearCol =
              FindNearestFluidColumn(world, gx, gz, false,
                                     kFluidColumnSearchRadius))
      {
        fluidCol = *nearCol;
        hasWater = true;
      }
    }
    if (hasWater)
    {
      if (const std::optional<float> feetY = FindAquaticSpawnFeetY(
              world, fluidCol.x, fluidCol.y, false, bodyHeight))
      {
        return glm::vec3(viewProbe.x, *feetY, viewProbe.z);
      }
    }
    if (const std::optional<float> feetY = world.QueryGroundFeetYColumn(gx, gz))
    {
      return glm::vec3(viewProbe.x, *feetY, viewProbe.z);
    }
    return viewProbe;
  }

  if (def.habitat == CreatureHabitat::Aerial)
  {
    if (const std::optional<float> feetY = world.QueryGroundFeetYColumn(gx, gz))
    {
      return glm::vec3(viewProbe.x, *feetY + 2.0f, viewProbe.z);
    }
    return viewProbe + glm::vec3(0.0f, 2.0f, 0.0f);
  }

  return viewProbe;
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

bool HabitatAllowsAt(const UWorld &world, CreatureHabitat habitat,
                     const glm::vec3 &bodyOrigin,
                     const glm::vec3 &sizeBlocks)
{
  const EnvironmentSample env =
      ProbeEnvironmentAt(world, bodyOrigin, sizeBlocks);
  return HabitatMatches(habitat, env);
}

bool HabitatAllowsMovementAt(const UWorld &world, CreatureHabitat habitat,
                             const glm::vec3 &bodyOrigin,
                             const glm::vec3 &sizeBlocks)
{
  const CollisionVolume vol = CollisionVolumeFromBody(bodyOrigin, sizeBlocks);
  if (world.CheckCollisionVolume(vol, 0))
  {
    return false;
  }
  if (habitat == CreatureHabitat::Aerial)
  {
    const EnvironmentSample env =
        ProbeEnvironmentAtEx(world, bodyOrigin, sizeBlocks, 0.25f);
    return !env.inFluid;
  }
  if (habitat == CreatureHabitat::Aquatic)
  {
    const EnvironmentSample env =
        ProbeEnvironmentAt(world, bodyOrigin, sizeBlocks);
    return env.inWater;
  }
  if (habitat == CreatureHabitat::Lava)
  {
    const EnvironmentSample env =
        ProbeEnvironmentAt(world, bodyOrigin, sizeBlocks);
    return env.inLava;
  }
  if (habitat == CreatureHabitat::Amphibious)
  {
    const EnvironmentSample env =
        ProbeEnvironmentAt(world, bodyOrigin, sizeBlocks);
    if (env.inWater)
    {
      return true;
    }
    return env.onSolidGround && !env.inFluid;
  }
  return HabitatAllowsAt(world, habitat, bodyOrigin, sizeBlocks);
}

bool HabitatAllowsAtForSpawn(const UWorld &world, CreatureHabitat habitat,
                             const glm::vec3 &bodyOrigin,
                             const glm::vec3 &sizeBlocks)
{
  if (habitat == CreatureHabitat::Aerial)
  {
    return HabitatAllowsMovementAt(world, habitat, bodyOrigin, sizeBlocks);
  }
  const EnvironmentSample env =
      ProbeEnvironmentAtEx(world, bodyOrigin, sizeBlocks, 0.5f);
  return HabitatMatches(habitat, env);
}

bool CanSpawnCreatureAt(const UWorld &world, const CreatureDefinition &def,
                        const glm::vec3 &bodyOrigin)
{
  const glm::vec3 size = def.bounds.restSizeBlocks;
  const glm::vec3 adjusted =
      AdjustSpawnBodyOrigin(world, def, bodyOrigin);
  if (!HabitatAllowsAtForSpawn(world, def.habitat, adjusted, size))
  {
    return false;
  }
  const CollisionVolume vol = CollisionVolumeFromBody(adjusted, size);
  if (world.CheckCreatureCollisionVolume(vol, 0))
  {
    return false;
  }
  return !world.CheckBlockCollisionVolume(vol);
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

std::string GetCreatureSpawnBlockedHint(const UWorld &world,
                                        const CreatureDefinition &def,
                                        const glm::vec3 &bodyOrigin)
{
  const glm::vec3 size = def.bounds.restSizeBlocks;
  const glm::vec3 adjusted = AdjustSpawnBodyOrigin(world, def, bodyOrigin);
  const EnvironmentSample env = ProbeEnvironmentAt(world, adjusted, size);
  if (def.habitat == CreatureHabitat::Terrestrial && !env.onSolidGround)
  {
    return u8"Нужна суша (моб в воздухе)";
  }
  if (def.habitat == CreatureHabitat::Aquatic)
  {
    const glm::ivec2 column = ResolveSpawnProbeColumn(world);
    if (!FindAquaticFeetY(world, column.x, column.y, false) &&
        !FindNearestFluidColumn(world, column.x, column.y, false,
                                kFluidColumnSearchRadius))
    {
      return u8"Нужна вода (нет воды под точкой)";
    }
    if (!env.inWater)
    {
      if (!FindAquaticSpawnFeetY(world, column.x, column.y, false, size.y))
      {
        return u8"Слишком мелкая вода";
      }
      return u8"Нужна вода";
    }
  }
  if (def.habitat == CreatureHabitat::Lava)
  {
    const glm::ivec2 column = ResolveSpawnProbeColumn(world);
    if (!FindAquaticFeetY(world, column.x, column.y, true) &&
        !FindNearestFluidColumn(world, column.x, column.y, true,
                                kFluidColumnSearchRadius))
    {
      return u8"Нужна лава";
    }
    if (!env.inLava)
    {
      return u8"Нужна лава";
    }
  }
  if (def.habitat == CreatureHabitat::Aerial && !HabitatMatches(def.habitat, env))
  {
    return u8"Нужно открытое небо";
  }
  if (!HabitatMatches(def.habitat, env))
  {
    return HabitatRequirementLabel(def.habitat);
  }
  const CollisionVolume vol = CollisionVolumeFromBody(adjusted, size);
  if (world.CheckCollisionVolume(vol, 0))
  {
    return u8"Нет места (коллизия)";
  }
  return {};
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
    if (const std::optional<float> feetY = FindAquaticSpawnFeetY(
            world, gx, gz, false, def.bounds.restSizeBlocks.y))
    {
      return glm::vec3(probeOrigin.x, *feetY, probeOrigin.z);
    }
    if (const std::optional<float> feetY =
            FindAquaticFeetY(world, gx, gz, false))
    {
      const float minFeet = *feetY;
      const float maxFeet =
          minFeet + static_cast<float>(255) - def.bounds.restSizeBlocks.y;
      const float feetYClamped = std::clamp(probeOrigin.y, minFeet, maxFeet);
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
    if (const std::optional<float> feetY = FindAquaticSpawnFeetY(
            world, gx, gz, true, def.bounds.restSizeBlocks.y))
    {
      return glm::vec3(probeOrigin.x, *feetY, probeOrigin.z);
    }
    if (const std::optional<float> feetY =
            FindAquaticFeetY(world, gx, gz, true))
    {
      const float minFeet = *feetY;
      const float maxFeet = minFeet + 32.0f;
      const float feetYClamped = std::clamp(probeOrigin.y, minFeet, maxFeet);
      return glm::vec3(probeOrigin.x, feetYClamped, probeOrigin.z);
    }
    return probeOrigin;
  }
  if (def.habitat == CreatureHabitat::Aerial)
  {
    const glm::vec3 size = def.bounds.restSizeBlocks;
    for (float dy = 2.0f; dy <= 16.0f; dy += 1.0f)
    {
      const glm::vec3 lifted = probeOrigin + glm::vec3(0.0f, dy, 0.0f);
      const EnvironmentSample env =
          ProbeEnvironmentAtEx(world, lifted, size, 0.5f);
      if (!HabitatMatches(CreatureHabitat::Aerial, env))
      {
        continue;
      }
      const CollisionVolume vol = CollisionVolumeFromBody(lifted, size);
      if (!world.CheckCollisionVolume(vol, 0))
      {
        return lifted;
      }
    }
    if (auto camera = world.GetCurrentUserCamera())
    {
      const glm::vec3 fallback = glm::vec3(probeOrigin.x, camera->GetPosition().y + 2.0f,
                                           probeOrigin.z);
      const EnvironmentSample env =
          ProbeEnvironmentAtEx(world, fallback, size, 0.5f);
      const CollisionVolume vol = CollisionVolumeFromBody(fallback, size);
      if (HabitatMatches(CreatureHabitat::Aerial, env) &&
          !world.CheckCollisionVolume(vol, 0))
      {
        return fallback;
      }
    }
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
