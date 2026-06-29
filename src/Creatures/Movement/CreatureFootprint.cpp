#include "Creatures/Movement/CreatureFootprint.h"

#include "Creatures/Core/CreatureBounds.h"
#include "World/Core/World.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"

namespace cutum
{

namespace
{

constexpr float kGroundSampleEpsilon = 0.04f;
constexpr float kSecondarySampleDrop = 0.15f;
constexpr float kGroundSkin = 0.02f;
constexpr float kMaxStepDownBlocks = 1.05f;
constexpr float kRaisedFootingDropMin = 0.45f;
constexpr int kMaxDepenetrateSteps = 16;

int CountSolidFootprintSamples(const UWorld &world, float feetY, float centerX,
                               float centerZ, float halfWidth, bool &centerSolid)
{
  const UBlockRegistry &registry = world.GetBlockRegistry();
  const UBlockWorld &blockWorld = world.GetBlockWorld();
  const int supportY = static_cast<int>(std::floor(feetY - kGroundSampleEpsilon));
  const int centerGx = WorldCoordToBlockIndex(centerX);
  const int centerGz = WorldCoordToBlockIndex(centerZ);
  const float sampleX[3] = {centerX - halfWidth, centerX, centerX + halfWidth};
  const float sampleZ[3] = {centerZ - halfWidth, centerZ, centerZ + halfWidth};
  int solidSamples = 0;
  centerSolid = false;
  for (float sx : sampleX)
  {
    for (float sz : sampleZ)
    {
      const glm::ivec3 cell(WorldCoordToBlockIndex(sx), supportY,
                            WorldCoordToBlockIndex(sz));
      if (!registry.BlocksMovement(blockWorld.GetBlock(cell)))
      {
        continue;
      }
      ++solidSamples;
      if (cell.x == centerGx && cell.z == centerGz)
      {
        centerSolid = true;
      }
    }
  }
  return solidSamples;
}

} // namespace

FootprintSample SampleCreatureFootprint(const UWorld &world,
                                        const glm::vec3 &bodyOrigin,
                                        const glm::vec3 &sizeBlocks)
{
  FootprintSample sample{};
  sample.totalSamples = 9;
  const CollisionVolume vol = CollisionVolumeFromBody(bodyOrigin, sizeBlocks);
  const float feetY = BoundsFeetY(bodyOrigin);
  const float halfWidth = CreatureFootprintHalfWidth(sizeBlocks);

  sample.solidSamples = CountSolidFootprintSamples(
      world, feetY, vol.center.x, vol.center.z, halfWidth, sample.centerSolid);
  if (!sample.centerSolid)
  {
    const float secondaryFeetY = feetY - kSecondarySampleDrop;
    bool secondaryCenter = false;
    const int secondarySolid = CountSolidFootprintSamples(
        world, secondaryFeetY, vol.center.x, vol.center.z, halfWidth,
        secondaryCenter);
    if (secondarySolid > sample.solidSamples)
    {
      sample.solidSamples = secondarySolid;
      sample.centerSolid = secondaryCenter;
    }
  }

  const float footprintWidth = std::max(sizeBlocks.x, sizeBlocks.z);
  sample.hasGroundSupport = EvaluateGroundSupport(
      sample.centerSolid, sample.solidSamples, footprintWidth);
  return sample;
}

void DepenetrateCreatureFromGround(const UWorld &world, glm::vec3 &bodyOrigin,
                                   const glm::vec3 &sizeBlocks,
                                   CreatureId skipCreatureId)
{
  for (int i = 0; i < kMaxDepenetrateSteps; ++i)
  {
    const CollisionVolume vol = CollisionVolumeFromBody(bodyOrigin, sizeBlocks);
    if (!world.CheckBlockCollisionVolume(vol))
    {
      return;
    }
    const FootprintSample footprint =
        SampleCreatureFootprint(world, bodyOrigin, sizeBlocks);
    if (!CreatureHasGroundSupport(footprint))
    {
      return;
    }
    bodyOrigin.y += kGroundSkin;
    const CollisionVolume trial = CollisionVolumeFromBody(bodyOrigin, sizeBlocks);
    if (!world.CheckBlockCollisionVolume(trial) &&
        !world.CheckCreatureCollisionVolume(trial, skipCreatureId))
    {
      return;
    }
  }
}

bool IsOnRaisedFooting(const UWorld &world, const glm::vec3 &bodyOrigin)
{
  const float feetY = BoundsFeetY(bodyOrigin);
  const int gx = WorldCoordToBlockIndex(bodyOrigin.x);
  const int gz = WorldCoordToBlockIndex(bodyOrigin.z);
  static const glm::ivec2 kNeighbors[] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (const glm::ivec2 &off : kNeighbors)
  {
    const std::optional<float> neighborGround =
        world.QueryGroundFeetYColumn(gx + off.x, gz + off.y);
    if (neighborGround && *neighborGround < feetY - kRaisedFootingDropMin)
    {
      return true;
    }
  }
  return false;
}

bool IsInDepression(const UWorld &world, const glm::vec3 &bodyOrigin)
{
  const float feetY = BoundsFeetY(bodyOrigin);
  const int gx = WorldCoordToBlockIndex(bodyOrigin.x);
  const int gz = WorldCoordToBlockIndex(bodyOrigin.z);
  static const glm::ivec2 kNeighbors[] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (const glm::ivec2 &off : kNeighbors)
  {
    const std::optional<float> neighborGround =
        world.QueryGroundFeetYColumn(gx + off.x, gz + off.y);
    if (neighborGround && *neighborGround > feetY + kRaisedFootingDropMin)
    {
      return true;
    }
  }
  return false;
}

glm::vec3 SnapBodyToColumnGround(const UWorld &world, glm::vec3 bodyOrigin,
                                 const glm::vec3 &sizeBlocks,
                                 CreatureId skipCreatureId,
                                 float maxDropBlocks)
{
  const float feetY = BoundsFeetY(bodyOrigin);
  const int gx = WorldCoordToBlockIndex(bodyOrigin.x);
  const int gz = WorldCoordToBlockIndex(bodyOrigin.z);
  const std::optional<float> groundY =
      world.QueryGroundFeetYUnder(gx, gz, feetY + 0.1f);
  if (!groundY)
  {
    return bodyOrigin;
  }
  const float drop = feetY - *groundY;
  if (drop < 0.02f || drop > maxDropBlocks + 0.01f)
  {
    return bodyOrigin;
  }
  const glm::vec3 trial(bodyOrigin.x, *groundY, bodyOrigin.z);
  const CollisionVolume vol = CollisionVolumeFromBody(trial, sizeBlocks);
  if (world.CheckBlockCollisionVolume(vol) ||
      world.CheckCreatureCollisionVolume(vol, skipCreatureId))
  {
    return bodyOrigin;
  }
  const FootprintSample footprint =
      SampleCreatureFootprint(world, trial, sizeBlocks);
  if (!CreatureHasGroundSupport(footprint))
  {
    return bodyOrigin;
  }
  return trial;
}

bool TryCreatureLedgeDrop(const UWorld &world, CreatureId skipCreatureId,
                          glm::vec3 &bodyOrigin, const glm::vec3 &horizDelta,
                          const glm::vec3 &sizeBlocks)
{
  const float feetY = BoundsFeetY(bodyOrigin);
  const int cx = WorldCoordToBlockIndex(bodyOrigin.x);
  const int cz = WorldCoordToBlockIndex(bodyOrigin.z);

  glm::vec3 pref(horizDelta.x, 0.0f, horizDelta.z);
  const float prefLen = glm::length(pref);
  if (prefLen > 1e-4f)
  {
    pref /= prefLen;
  }

  static const glm::ivec2 kCardinals[] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  int order[4] = {0, 1, 2, 3};
  for (int i = 0; i < 4; ++i)
  {
    int best = i;
    float bestDot = -2.0f;
    for (int j = i; j < 4; ++j)
    {
      const glm::ivec2 &off = kCardinals[order[j]];
      const glm::vec3 dir(static_cast<float>(off.x), 0.0f,
                          static_cast<float>(off.y));
      const float dot = glm::dot(dir, pref);
      if (dot > bestDot)
      {
        bestDot = dot;
        best = j;
      }
    }
    const int tmp = order[i];
    order[i] = order[best];
    order[best] = tmp;
  }

  const float nudge =
      std::max(0.35f, CreatureFootprintHalfWidth(sizeBlocks) * 0.85f);
  float bestDrop = 0.0f;
  glm::vec3 bestPos = bodyOrigin;

  for (int oi = 0; oi < 4; ++oi)
  {
    const glm::ivec2 &off = kCardinals[order[oi]];
    const std::optional<float> neighborGround =
        world.QueryGroundFeetYColumn(cx + off.x, cz + off.y);
    if (!neighborGround || *neighborGround > feetY - kRaisedFootingDropMin)
    {
      continue;
    }
    glm::vec3 trial(bodyOrigin.x + static_cast<float>(off.x) * nudge,
                    bodyOrigin.y,
                    bodyOrigin.z + static_cast<float>(off.y) * nudge);
    glm::vec3 snapped = SnapBodyToColumnGround(world, trial, sizeBlocks,
                                               skipCreatureId,
                                               kMaxStepDownBlocks);
    const float drop = bodyOrigin.y - snapped.y;
    if (drop < kRaisedFootingDropMin)
    {
      continue;
    }
    const FootprintSample footprint =
        SampleCreatureFootprint(world, snapped, sizeBlocks);
    if (!CreatureHasGroundSupport(footprint))
    {
      continue;
    }
    if (drop > bestDrop)
    {
      bestDrop = drop;
      bestPos = snapped;
    }
  }

  if (bestDrop < kRaisedFootingDropMin)
  {
    return false;
  }
  bodyOrigin = bestPos;
  return true;
}

} // namespace cutum
