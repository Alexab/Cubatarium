#include "Creatures/Environment/CreatureEnvironment.h"
#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Environment/CreatureTraverseQueries.h"
#include "Navigation/NavigationTypes.h"
#include "Navigation/WorldNavigationQueries.h"
#include "Render/Camera/Camera.h"
#include "Render/Primitives/Cube.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"
#include <chrono>
#include <unordered_map>
#include "World/Raycast/BlockRaycast.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace cutum
{
namespace
{

constexpr float kDefaultViewerEyeHeight = 1.62f;
constexpr float kSpawnAheadBlocks = 3.0f;
constexpr int kFluidColumnSearchRadius = 4;

std::optional<float> ResolveTerrestrialGroundFeetY(const UWorld &world, int gx,
                                                   int gz,
                                                   float referenceFeetY)
{
  if (const std::optional<float> feetY =
          world.QueryGroundFeetYUnder(gx, gz, referenceFeetY))
  {
    return feetY;
  }
  return world.QueryGroundFeetYColumn(gx, gz);
}

bool IsPassableBlock(const UBlockRegistry &registry, BlockId id)
{
  return id != BLOCK_AIR && !registry.BlocksMovement(id);
}

enum class FluidKind : uint8_t
{
  None,
  Water,
  Lava,
};

FluidKind ClassifyFluidBlock(const UBlockRegistry &registry, BlockId id)
{
  // Fluid identity by name/render style, memoized per block id: the raw
  // string find used to run inside every probe of every scanned block
  // (audit S9). World sim is single-threaded; a benign recompute race
  // yields the same value.
  static std::unordered_map<const void *,
                            std::unordered_map<BlockId, FluidKind>>
      cache;
  auto &perRegistry = cache[&registry];
  if (const auto it = perRegistry.find(id); it != perRegistry.end())
  {
    return it->second;
  }
  FluidKind kind = FluidKind::None;
  if (IsPassableBlock(registry, id))
  {
    const std::string &name = registry.GetTypeNameById(id);
    if (name.find("lava") != std::string::npos)
    {
      kind = FluidKind::Lava;
    }
    else if (registry.GetRenderStyle(id) == BlockRenderStyle::Fluid ||
             name.find("water") != std::string::npos)
    {
      kind = FluidKind::Water;
    }
  }
  perRegistry[id] = kind;
  return kind;
}

bool IsLavaBlock(const UBlockRegistry &registry, BlockId id)
{
  return ClassifyFluidBlock(registry, id) == FluidKind::Lava;
}

bool IsWaterBlock(const UBlockRegistry &registry, BlockId id)
{
  return ClassifyFluidBlock(registry, id) == FluidKind::Water;
}

void SampleHabitatFluidsInVolume(const UWorld &world, const CollisionVolume &vol,
                                 bool &outWater, bool &outLava)
{
  outWater = false;
  outLava = false;
  const UBlockRegistry &registry = world.GetBlockRegistry();
  const UBlockWorld &blockWorld = world.GetBlockWorld();
  const glm::vec3 center = vol.center;
  const glm::vec3 half = vol.halfExtents;
  const glm::ivec3 blockCenterCell = WorldPosToBlock(center);
  const int radius =
      static_cast<int>(std::ceil(std::max({half.x, half.y, half.z})));
  const glm::vec3 blockHalf(0.5f);
  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dy = -radius; dy <= radius; ++dy)
    {
      for (int dz = -radius; dz <= radius; ++dz)
      {
        const glm::ivec3 blockPos = blockCenterCell + glm::ivec3(dx, dy, dz);
        const BlockId id = blockWorld.GetBlock(blockPos);
        if (!IsPassableBlock(registry, id))
        {
          continue;
        }
        const glm::vec3 blockCenter = BlockCenter(blockPos);
        if (!UCube::CheckAabbCollision(center, half, blockCenter, blockHalf))
        {
          continue;
        }
        if (IsLavaBlock(registry, id))
        {
          outLava = true;
        }
        else if (IsWaterBlock(registry, id))
        {
          outWater = true;
        }
      }
    }
  }
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

// Position-quantized memo for the environment probe (audit M2): the
// O(r^3) volume scan used to run per creature per physics frame from
// vitals, movement habitat gates and locomotion facts. Entries expire
// after kProbeTtlSec, so block and fluid changes surface within that
// bound; spawn-time decisions keep reading fresh (ForSpawn uses its own
// clearance, aerial delegates here with at most one TTL of staleness).
struct ProbeMemoKey
{
  int32_t x{0}, y{0}, z{0};
  int32_t sx{0}, sy{0}, sz{0};
  bool operator==(const ProbeMemoKey &o) const
  {
    return x == o.x && y == o.y && z == o.z && sx == o.sx && sy == o.sy &&
           sz == o.sz;
  }
};

struct ProbeMemoKeyHash
{
  size_t operator()(const ProbeMemoKey &k) const
  {
    size_t h = 1469598103934665603ull;
    for (int32_t v : {k.x, k.y, k.z, k.sx, k.sy, k.sz})
    {
      h ^= static_cast<uint32_t>(v);
      h *= 1099511628211ull;
    }
    return h;
  }
};

constexpr float kProbeTtlSec = 0.2f;
constexpr size_t kProbeMemoMaxEntries = 8192;
std::unordered_map<ProbeMemoKey, std::pair<EnvironmentSample,
                                           std::chrono::steady_clock::time_point>,
                   ProbeMemoKeyHash>
    gProbeMemo;

int32_t QuantizeProbeCoord(float v)
{
  return static_cast<int32_t>(std::floor(v * 4.0f));
}

EnvironmentSample ProbeEnvironmentAtEx(const UWorld &world,
                                       const glm::vec3 &bodyOrigin,
                                       const glm::vec3 &sizeBlocks,
                                       float aerialClearance)
{
  EnvironmentSample env;
  const CollisionVolume vol = CollisionVolumeFromBody(bodyOrigin, sizeBlocks);
  SampleHabitatFluidsInVolume(world, vol, env.inWater, env.inLava);
  env.inFluid = env.inWater || env.inLava;
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

EnvironmentSample ProbeEnvironmentAtMemoized(const UWorld &world,
                                             const glm::vec3 &bodyOrigin,
                                             const glm::vec3 &sizeBlocks)
{
  const ProbeMemoKey key{QuantizeProbeCoord(bodyOrigin.x),
                         QuantizeProbeCoord(bodyOrigin.y),
                         QuantizeProbeCoord(bodyOrigin.z),
                         QuantizeProbeCoord(sizeBlocks.x),
                         QuantizeProbeCoord(sizeBlocks.y),
                         QuantizeProbeCoord(sizeBlocks.z)};
  const auto now = std::chrono::steady_clock::now();
  if (const auto it = gProbeMemo.find(key); it != gProbeMemo.end())
  {
    if (std::chrono::duration<float>(now - it->second.second).count() <
        kProbeTtlSec)
    {
      return it->second.first;
    }
  }
  const EnvironmentSample sample =
      ProbeEnvironmentAtEx(world, bodyOrigin, sizeBlocks, 1.0f);
  if (gProbeMemo.size() >= kProbeMemoMaxEntries)
  {
    gProbeMemo.clear();
  }
  gProbeMemo[key] = {sample, now};
  return sample;
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
            ResolveTerrestrialGroundFeetY(world, probeGx, probeGz, viewProbe.y))
    {
      return glm::vec3(viewProbe.x, *feetY, viewProbe.z);
    }
    return viewProbe;
  }

  if (def.habitat == CreatureHabitat::Aquatic)
  {
    const int probeGx = WorldCoordToBlockIndex(viewProbe.x);
    const int probeGz = WorldCoordToBlockIndex(viewProbe.z);
    glm::ivec2 fluidCol(probeGx, probeGz);
    if (!FindAquaticFeetY(world, probeGx, probeGz, false))
    {
      if (const std::optional<glm::ivec2> nearCol =
              FindNearestFluidColumn(world, probeGx, probeGz, false,
                                     kFluidColumnSearchRadius))
      {
        fluidCol = *nearCol;
      }
      else if (const std::optional<glm::ivec2> rayCol =
                   FindNearestFluidColumn(world, gx, gz, false,
                                          kFluidColumnSearchRadius))
      {
        fluidCol = *rayCol;
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
    const int probeGx = WorldCoordToBlockIndex(viewProbe.x);
    const int probeGz = WorldCoordToBlockIndex(viewProbe.z);
    glm::ivec2 fluidCol(probeGx, probeGz);
    if (!FindAquaticFeetY(world, probeGx, probeGz, true))
    {
      if (const std::optional<glm::ivec2> nearCol =
              FindNearestFluidColumn(world, probeGx, probeGz, true,
                                     kFluidColumnSearchRadius))
      {
        fluidCol = *nearCol;
      }
      else if (const std::optional<glm::ivec2> rayCol =
                   FindNearestFluidColumn(world, gx, gz, true,
                                          kFluidColumnSearchRadius))
      {
        fluidCol = *rayCol;
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
    const int probeGx = WorldCoordToBlockIndex(viewProbe.x);
    const int probeGz = WorldCoordToBlockIndex(viewProbe.z);
    glm::ivec2 fluidCol(probeGx, probeGz);
    bool hasWater = static_cast<bool>(FindAquaticFeetY(world, probeGx, probeGz, false));
    if (!hasWater)
    {
      if (const std::optional<glm::ivec2> nearCol =
              FindNearestFluidColumn(world, probeGx, probeGz, false,
                                     kFluidColumnSearchRadius))
      {
        fluidCol = *nearCol;
        hasWater = true;
      }
      else if (const std::optional<glm::ivec2> rayCol =
                   FindNearestFluidColumn(world, gx, gz, false,
                                          kFluidColumnSearchRadius))
      {
        fluidCol = *rayCol;
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
    if (const std::optional<float> feetY = ResolveTerrestrialGroundFeetY(
            world, WorldCoordToBlockIndex(viewProbe.x),
            WorldCoordToBlockIndex(viewProbe.z), viewProbe.y))
    {
      return glm::vec3(viewProbe.x, *feetY, viewProbe.z);
    }
    return viewProbe;
  }

  if (def.habitat == CreatureHabitat::Aerial)
  {
    const int probeGx = WorldCoordToBlockIndex(viewProbe.x);
    const int probeGz = WorldCoordToBlockIndex(viewProbe.z);
    if (const std::optional<float> feetY =
            world.QueryGroundFeetYColumn(probeGx, probeGz))
    {
      return glm::vec3(viewProbe.x, *feetY + 1.25f, viewProbe.z);
    }
    return glm::vec3(viewProbe.x, viewProbe.y + 1.25f, viewProbe.z);
  }

  return viewProbe;
}

bool CanCreatureOccupyAt(const UWorld &world, CreatureHabitat habitat,
                         const glm::vec3 &bodyOrigin,
                         const glm::vec3 &sizeBlocks)
{
  const EnvironmentSample env =
      ProbeEnvironmentAtMemoized(world, bodyOrigin, sizeBlocks);
  if (!HabitatMatches(habitat, env))
  {
    return false;
  }
  const CollisionVolume vol = CollisionVolumeFromBody(bodyOrigin, sizeBlocks);
  return !world.CheckCollisionVolume(vol, 0);
}

namespace
{

bool TerrestrialMobStandValid(const UWorld &world, const glm::vec3 &body_origin,
                            const glm::vec3 &size_blocks,
                            uint64_t skip_creature_id)
{
  const UWorldNavigationQueries navigation(world);
  const NavigationStandNode node =
      NavigationStandNodeFromBody(body_origin);
  if (!navigation.IsTerrestrialStandNode(node, size_blocks.y))
  {
    return false;
  }
  if (!HabitatAllowsMovementAt(world, CreatureHabitat::Terrestrial, body_origin,
                               size_blocks))
  {
    return false;
  }
  const CollisionVolume vol =
      CollisionVolumeFromBody(body_origin, size_blocks);
  return !world.CheckCreatureCollisionVolume(vol, skip_creature_id);
}

} // namespace

glm::vec3 ResolveTerrestrialMobMovement(const UWorld &world,
                                        const glm::vec3 &bodyOrigin,
                                        const glm::vec3 &horizontalDelta,
                                        const glm::vec3 &sizeBlocks,
                                        uint64_t skip_creature_id,
                                        float max_step_up, float max_step_down)
{
  const glm::vec3 horiz(horizontalDelta.x, 0.0f, horizontalDelta.z);
  if (glm::dot(horiz, horiz) < 1e-10f)
  {
    return bodyOrigin;
  }

  std::vector<float> y_offsets;
  y_offsets.push_back(0.0f);
  if (max_step_up > 0.01f)
  {
    y_offsets.push_back(max_step_up);
  }
  const int drop_steps =
      static_cast<int>(std::ceil(std::max(max_step_down, 1.0f)));
  for (int step = 1; step <= drop_steps; ++step)
  {
    y_offsets.push_back(-static_cast<float>(step));
  }

  glm::vec3 best = bodyOrigin;
  float best_travel_sq = 0.0f;
  for (const float y_offset : y_offsets)
  {
    glm::vec3 trial = bodyOrigin;
    trial.y += y_offset;
    const glm::vec3 moved =
        world.ResolveMovementBody(trial, horiz, sizeBlocks, skip_creature_id);
    const glm::vec2 xz_delta(moved.x - bodyOrigin.x, moved.z - bodyOrigin.z);
    const float travel_sq = glm::dot(xz_delta, xz_delta);
    if (travel_sq < 1e-8f)
    {
      continue;
    }

    const int gx = WorldCoordToBlockIndex(moved.x);
    const int gz = WorldCoordToBlockIndex(moved.z);
    const std::optional<float> feet_y =
        ResolveTerrestrialGroundFeetY(world, gx, gz, moved.y);
    if (!feet_y)
    {
      continue;
    }

    const float climb = *feet_y - bodyOrigin.y;
    const float drop = bodyOrigin.y - *feet_y;
    if (climb > max_step_up + 0.05f || drop > max_step_down + 0.05f)
    {
      continue;
    }

    const glm::vec3 snapped(moved.x, *feet_y, moved.z);
    if (!TerrestrialMobStandValid(world, snapped, sizeBlocks, skip_creature_id))
    {
      continue;
    }

    if (travel_sq > best_travel_sq)
    {
      best_travel_sq = travel_sq;
      best = snapped;
    }
  }
  return best;
}

bool HabitatAllowsAt(const UWorld &world, CreatureHabitat habitat,
                     const glm::vec3 &bodyOrigin,
                     const glm::vec3 &sizeBlocks)
{
  const EnvironmentSample env =
      ProbeEnvironmentAtMemoized(world, bodyOrigin, sizeBlocks);
  return HabitatMatches(habitat, env);
}

bool HabitatAllowsMovementAt(const UWorld &world, CreatureHabitat habitat,
                             const glm::vec3 &bodyOrigin,
                             const glm::vec3 &sizeBlocks,
                             float maxClimbDropBlocks)
{
  if (habitat == CreatureHabitat::Terrestrial)
  {
    // Shared stand predicate with A* node / probes (actual body, not footprint).
    return CanCreatureStandAt(world, bodyOrigin, sizeBlocks, maxClimbDropBlocks);
  }
  if (habitat == CreatureHabitat::Amphibious)
  {
    const EnvironmentSample env =
        ProbeEnvironmentAtMemoized(world, bodyOrigin, sizeBlocks);
    if (env.inWater)
    {
      return true;
    }
    return env.onSolidGround && !env.inFluid;
  }
  if (habitat == CreatureHabitat::Aquatic)
  {
    const EnvironmentSample env =
        ProbeEnvironmentAtMemoized(world, bodyOrigin, sizeBlocks);
    return env.inWater;
  }
  if (habitat == CreatureHabitat::Lava)
  {
    const EnvironmentSample env =
        ProbeEnvironmentAtMemoized(world, bodyOrigin, sizeBlocks);
    return env.inLava;
  }
  const CollisionVolume vol = CollisionVolumeFromBody(bodyOrigin, sizeBlocks);
  if (world.CheckBlockCollisionVolume(vol))
  {
    return false;
  }
  if (habitat == CreatureHabitat::Aerial)
  {
    return !world.CheckBlockCollisionVolume(vol);
  }
  return HabitatAllowsAt(world, habitat, bodyOrigin, sizeBlocks);
}

bool HabitatAllowsAtForSpawn(const UWorld &world, CreatureHabitat habitat,
                             const glm::vec3 &bodyOrigin,
                             const glm::vec3 &sizeBlocks)
{
  if (habitat == CreatureHabitat::Aerial)
  {
    return HabitatAllowsAt(world, habitat, bodyOrigin, sizeBlocks);
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
  // Slightly shrink spawn AABB so tall mobs (zombie) fit under low ceilings.
  const CollisionVolume vol =
      InsetCollisionVolumeForSpawn(CollisionVolumeFromBody(adjusted, size));
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
      return u8"Нужна вода (на траве/суше не получится)";
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
      return u8"Нужна лава (на траве/суше не получится)";
    }
    if (!env.inLava)
    {
      return u8"Нужна лава (на траве/суше не получится)";
    }
  }
  if (def.habitat == CreatureHabitat::Aerial)
  {
    const CollisionVolume vol = CollisionVolumeFromBody(adjusted, size);
    if (world.CheckCollisionVolume(vol, 0))
    {
      return u8"Нет места (коллизия)";
    }
    if (!HabitatAllowsMovementAt(world, CreatureHabitat::Aerial, adjusted,
                                 size))
    {
      return u8"Нужно открытое пространство";
    }
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
    const int gx = WorldCoordToBlockIndex(probeOrigin.x);
    const int gz = WorldCoordToBlockIndex(probeOrigin.z);
    float baseY = probeOrigin.y;
    if (const std::optional<float> feetY = world.QueryGroundFeetYColumn(gx, gz))
    {
      baseY = std::max(baseY, *feetY + 1.25f);
    }
    const glm::vec3 base(probeOrigin.x, baseY, probeOrigin.z);
    for (float dy = 0.0f; dy <= 2.0f; dy += 0.5f)
    {
      const glm::vec3 lifted = base + glm::vec3(0.0f, dy, 0.0f);
      const CollisionVolume vol = CollisionVolumeFromBody(lifted, size);
      if (!world.CheckBlockCollisionVolume(vol))
      {
        return lifted;
      }
    }
    return base;
  }
  if (def.habitat == CreatureHabitat::Terrestrial)
  {
    const int gx = WorldCoordToBlockIndex(probeOrigin.x);
    const int gz = WorldCoordToBlockIndex(probeOrigin.z);
    if (const std::optional<float> feetY =
            ResolveTerrestrialGroundFeetY(world, gx, gz, probeOrigin.y))
    {
      return glm::vec3(probeOrigin.x, *feetY, probeOrigin.z);
    }
  }
  return probeOrigin;
}

glm::vec3 TryDepenetrateSpawnOrigin(const UWorld &world, CreatureHabitat habitat,
                                    const glm::vec3 &bodyOrigin,
                                    const glm::vec3 &sizeBlocks,
                                    uint64_t skip_creature_id)
{
  CollisionVolume vol = CollisionVolumeFromBody(bodyOrigin, sizeBlocks);
  if (!world.CheckCreatureCollisionVolume(vol, skip_creature_id))
  {
    return bodyOrigin;
  }
  constexpr float kStep = 0.25f;
  const glm::vec2 dirs[] = {{1.0f, 0.0f},  {-1.0f, 0.0f}, {0.0f, 1.0f},
                            {0.0f, -1.0f}, {0.707f, 0.707f}, {-0.707f, 0.707f},
                            {0.707f, -0.707f}, {-0.707f, -0.707f}};
  for (int ring = 1; ring <= 3; ++ring)
  {
    const float offset = kStep * static_cast<float>(ring);
    for (const glm::vec2 &dir : dirs)
    {
      const glm::vec3 candidate =
          bodyOrigin + glm::vec3(dir.x * offset, 0.0f, dir.y * offset);
      vol = CollisionVolumeFromBody(candidate, sizeBlocks);
      if (world.CheckCreatureCollisionVolume(vol, skip_creature_id))
      {
        continue;
      }
      if (!HabitatAllowsAt(world, habitat, candidate, sizeBlocks))
      {
        continue;
      }
      return candidate;
    }
  }
  return bodyOrigin;
}

void ApplyEnvironmentLocomotionFacts(const UWorld &world,
                                     const glm::vec3 &bodyOrigin,
                                     const glm::vec3 &sizeBlocks,
                                     CreatureLocomotionFacts &facts)
{
  const EnvironmentSample env =
      ProbeEnvironmentAtMemoized(world, bodyOrigin, sizeBlocks);
  facts.inFluid = env.inWater || env.inLava;
  facts.onFluidBottom = env.inFluid && env.onSolidGround;
}

} // namespace cutum
