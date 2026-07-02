#include "World/Collision/WorldCollision.h"
#include "World/Collision/VoxelDdaTraversal.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Render/Primitives/Cube.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Environment/WorldEnvironment.h"
#include "World/Math/GridMath.h"
#include "World/Physics/PhysicsTelemetry.h"
#include "World/Raycast/BlockRaycast.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

constexpr int kFootprintMinSolidSamples = 4;
constexpr int kSubchunkSize = 4;

int SubchunkBitIndex(glm::ivec3 local)
{
  const int sx = local.x / kSubchunkSize;
  const int sy = local.y / kSubchunkSize;
  const int sz = local.z / kSubchunkSize;
  return sx + sy * 4 + sz * 16;
}

bool MaskHasSubchunk(uint64_t mask, int bit_index)
{
  if (bit_index < 0 || bit_index >= 64)
  {
    return false;
  }
  return (mask & (1ull << bit_index)) != 0;
}

struct FootprintStandSampleStats
{
  int solidSamples{0};
  int validStandSamples{0};
  bool centerSolid{false};
  bool centerValidStand{false};
};

FootprintStandSampleStats
SampleFootprintAtFeet(const UWorldCollision &collision,
                      const UBlockWorld &blockWorld,
                      const UBlockRegistry &registry, float feetY, float centerX,
                      float centerZ, float halfWidth, const PlayerCapsule &cap,
                      bool checkValidStand)
{
  FootprintStandSampleStats stats{};
  const int supportY = static_cast<int>(std::floor(feetY - 0.04f));
  const int centerGx = WorldCoordToBlockIndex(centerX);
  const int centerGz = WorldCoordToBlockIndex(centerZ);
  const float sampleX[3] = {centerX - halfWidth, centerX, centerX + halfWidth};
  const float sampleZ[3] = {centerZ - halfWidth, centerZ, centerZ + halfWidth};
  for (float sx : sampleX)
  {
    for (float sz : sampleZ)
    {
      const glm::ivec3 cell(WorldCoordToBlockIndex(sx), supportY,
                            WorldCoordToBlockIndex(sz));
      const bool solid = registry.BlocksMovement(blockWorld.GetBlock(cell));
      if (!solid)
      {
        continue;
      }
      ++stats.solidSamples;
      const bool isCenter = cell.x == centerGx && cell.z == centerGz;
      if (isCenter)
      {
        stats.centerSolid = true;
      }
      if (!checkValidStand)
      {
        continue;
      }
      if (!collision.IsValidStandCell(cell, cap))
      {
        continue;
      }
      ++stats.validStandSamples;
      if (isCenter)
      {
        stats.centerValidStand = true;
      }
    }
  }
  return stats;
}

constexpr float kCollisionMaxStep = 0.25f;
constexpr float kCollisionEpsilon = 0.01f;
constexpr int kCollisionMaxIterations = 64;

glm::vec3 ResolveMovementAxisDda(const UWorldCollision &collision,
                                  const glm::vec3 &fromBody, float axisDelta,
                                  int axis, const glm::vec3 &currentSizeBlocks,
                                  CreatureId skipCreatureId)
{
  const float sign = axisDelta > 0.0f ? 1.0f : -1.0f;
  const float remaining = std::abs(axisDelta);
  glm::vec3 axisUnit(0.0f);
  axisUnit[axis] = sign;

  const glm::vec3 targetBody = fromBody + axisUnit * axisDelta;
  if (!collision.CheckCollisionVolume(
          CollisionVolumeFromBody(targetBody, currentSizeBlocks),
          skipCreatureId))
  {
    return targetBody;
  }

  glm::vec3 body = fromBody;
  const glm::vec3 half = currentSizeBlocks * 0.5f;
  glm::vec3 rayOrigin = fromBody;
  rayOrigin[axis] += half[axis] * sign;

  float traveled = 0.0f;
  bool blocked = false;
  TraverseVoxelRay(rayOrigin, axisUnit * sign, remaining,
                   [&](glm::ivec3 /*cell*/)
                   {
                     traveled += 1.0f;
                     if (traveled > remaining)
                     {
                       return true;
                     }
                     const glm::vec3 testBody = fromBody + axisUnit * traveled * sign;
                     if (collision.CheckCollisionVolume(
                             CollisionVolumeFromBody(testBody, currentSizeBlocks),
                             skipCreatureId))
                     {
                       blocked = true;
                       glm::vec3 lo = body;
                       glm::vec3 hi = testBody;
                       for (int i = 0; i < 8; ++i)
                       {
                         const glm::vec3 mid = (lo + hi) * 0.5f;
                         if (collision.CheckCollisionVolume(
                                 CollisionVolumeFromBody(mid, currentSizeBlocks),
                                 skipCreatureId))
                         {
                           hi = mid;
                         }
                         else
                         {
                           lo = mid;
                         }
                       }
                       if (axis == 1 && sign < 0.0f)
                       {
                         body = lo;
                       }
                       else
                       {
                         body = lo - axisUnit * kCollisionEpsilon * sign;
                       }
                       return true;
                     }
                     body = testBody;
                     return false;
                   });

  if (!blocked)
  {
    return targetBody;
  }
  return body;
}

glm::vec3 ResolveMovementAxisBody(const UWorldCollision &collision,
                                  const glm::vec3 &fromBody, float axisDelta,
                                  int axis, const glm::vec3 &currentSizeBlocks,
                                  CreatureId skipCreatureId)
{
  if (std::abs(axisDelta) < 1e-8f)
  {
    return fromBody;
  }
  const float remaining = std::abs(axisDelta);
  if (collision.IsCollisionDdaEnabled() && remaining >= 1.0f)
  {
    return ResolveMovementAxisDda(collision, fromBody, axisDelta, axis,
                                  currentSizeBlocks, skipCreatureId);
  }
  const float sign = axisDelta > 0.0f ? 1.0f : -1.0f;
  float movable = remaining;
  glm::vec3 body = fromBody;
  glm::vec3 axisUnit(0.0f);
  axisUnit[axis] = 1.0f;

  int iterations = 0;
  while (movable > 1e-6f && iterations < kCollisionMaxIterations)
  {
    const float step = std::min(movable, kCollisionMaxStep);
    const glm::vec3 nextBody = body + axisUnit * step * sign;
    if (collision.CheckCollisionVolume(
            CollisionVolumeFromBody(nextBody, currentSizeBlocks),
            skipCreatureId))
    {
      glm::vec3 lo = body;
      glm::vec3 hi = nextBody;
      for (int i = 0; i < 8; ++i)
      {
        const glm::vec3 mid = (lo + hi) * 0.5f;
        if (collision.CheckCollisionVolume(
                CollisionVolumeFromBody(mid, currentSizeBlocks),
                skipCreatureId))
        {
          hi = mid;
        }
        else
        {
          lo = mid;
        }
      }
      if (axis == 1 && sign < 0.0f)
      {
        body = lo;
      }
      else
      {
        body = lo - axisUnit * kCollisionEpsilon * sign;
      }
      break;
    }
    body = nextBody;
    movable -= step;
    ++iterations;
  }
  return body;
}

glm::vec3 StepStandPosition(const glm::ivec3 &stepCell,
                            const PlayerCapsule &cap)
{
  const float feetY = BlockTopY(stepCell.y);
  return glm::vec3(static_cast<float>(stepCell.x), feetY + cap.eyeHeight,
                   static_cast<float>(stepCell.z));
}

bool FindSteppableLedge(const UWorldCollision &collision,
                        const UBlockWorld &blockWorld,
                        const UBlockRegistry &registry, const glm::vec3 &eyePos,
                        const glm::vec3 &dir, const PlayerCapsule &cap,
                        CreatureId skipCreatureId, glm::ivec3 &outStepCell)
{
  const int dx = dir.x > 0.25f ? 1 : (dir.x < -0.25f ? -1 : 0);
  const int dz = dir.z > 0.25f ? 1 : (dir.z < -0.25f ? -1 : 0);
  if (dx == 0 && dz == 0)
  {
    return false;
  }
  const float feetY = cap.feetY(eyePos);
  const int supportY = static_cast<int>(std::floor(feetY - 0.04f));
  const glm::ivec3 standCell(WorldCoordToBlockIndex(eyePos.x), supportY,
                             WorldCoordToBlockIndex(eyePos.z));
  if (!registry.BlocksMovement(blockWorld.GetBlock(standCell)))
  {
    return false;
  }
  const glm::ivec3 riserCell(standCell.x + dx, supportY, standCell.z + dz);
  if (!registry.BlocksMovement(blockWorld.GetBlock(riserCell)))
  {
    return false;
  }
  const glm::ivec3 stepCell(standCell.x + dx, supportY + 1, standCell.z + dz);
  if (!collision.IsValidStandCell(stepCell, cap))
  {
    return false;
  }
  const float stepFeetY = BlockTopY(stepCell.y);
  const float rise = stepFeetY - feetY;
  if (rise < 0.45f || rise > 1.05f)
  {
    return false;
  }
  const glm::vec3 landingEye = StepStandPosition(stepCell, cap);
  if (collision.CheckCollision(landingEye, cap, skipCreatureId))
  {
    return false;
  }
  outStepCell = stepCell;
  return true;
}

float DistanceToStepRiser(const glm::vec3 &eyePos, const glm::ivec3 &stepCell,
                          const glm::vec3 &dir, const PlayerCapsule &cap)
{
  const glm::vec3 blockCenter(static_cast<float>(stepCell.x),
                              static_cast<float>(stepCell.y),
                              static_cast<float>(stepCell.z));
  const glm::vec3 facePoint =
      blockCenter - glm::vec3(dir.x * 0.5f, 0.0f, dir.z * 0.5f);
  const glm::vec3 playerLead =
      eyePos + glm::vec3(dir.x * cap.halfWidth, 0.0f, dir.z * cap.halfWidth);
  return glm::dot(facePoint - playerLead, dir);
}

} // namespace

UWorldCollision::UWorldCollision(UBlockWorld &blockWorld,
                               UWorldEnvironment *environment)
    : BlockWorld(blockWorld), Environment(environment)
{
}

std::optional<int> UWorldCollision::FindHighestSolidY(int x, int z) const
{
  if (!BlockRegistry)
  {
    return std::nullopt;
  }
  for (int y = 255; y >= 0; --y)
  {
    if (BlockRegistry->IsSolid(BlockWorld.GetBlock(glm::ivec3(x, y, z))))
    {
      return y;
    }
  }
  return std::nullopt;
}

std::optional<float> UWorldCollision::QueryGroundFeetYColumn(int worldX,
                                                             int worldZ) const
{
  if (const std::optional<int> topY = FindHighestSolidY(worldX, worldZ))
  {
    return BlockTopY(*topY);
  }
  return std::nullopt;
}

std::optional<float> UWorldCollision::QueryGroundFeetYUnder(int worldX,
                                                              int worldZ,
                                                              float referenceFeetY) const
{
  if (!BlockRegistry)
  {
    return std::nullopt;
  }
  const int startY =
      std::clamp(static_cast<int>(std::floor(referenceFeetY + 0.25f)), 0, 255);
  for (int y = startY; y >= 0; --y)
  {
    if (BlockRegistry->IsSolid(
            BlockWorld.GetBlock(glm::ivec3(worldX, y, worldZ))))
    {
      return BlockTopY(y);
    }
  }
  return std::nullopt;
}

bool UWorldCollision::IsValidStandCell(const glm::ivec3 &cell,
                                       const PlayerCapsule &cap) const
{
  if (!BlockRegistry)
  {
    return false;
  }
  if (!BlockRegistry->BlocksMovement(BlockWorld.GetBlock(cell)))
  {
    return false;
  }
  const std::optional<int> columnTopY = FindHighestSolidY(cell.x, cell.z);
  if (!columnTopY || *columnTopY != cell.y)
  {
    return false;
  }
  const int layers = static_cast<int>(std::ceil(cap.height));
  for (int dy = 1; dy <= layers; ++dy)
  {
    const glm::ivec3 above(cell.x, cell.y + dy, cell.z);
    if (BlockRegistry->BlocksMovement(BlockWorld.GetBlock(above)))
    {
      return false;
    }
  }
  return true;
}

bool UWorldCollision::IsValidStandFootprint(const glm::vec3 &eyePos,
                                            const PlayerCapsule &cap,
                                            float feetY) const
{
  if (!BlockRegistry)
  {
    return false;
  }
  const CollisionVolume vol = CollisionVolumeFromEye(eyePos, cap);
  const FootprintStandSampleStats stats = SampleFootprintAtFeet(
      *this, BlockWorld, *BlockRegistry, feetY, vol.center.x, vol.center.z,
      cap.halfWidth, cap, true);
  return stats.centerValidStand &&
         stats.validStandSamples >= kFootprintMinSolidSamples;
}

bool UWorldCollision::CheckBlockCollisionVolume(const CollisionVolume &vol) const
{
  if (!BlockRegistry)
  {
    return false;
  }
  if (BroadphaseEnabled && !MayContainSolid(vol))
  {
    if (Telemetry)
    {
      ++Telemetry->CollisionBroadphaseRejects;
    }
    return false;
  }
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
        const BlockId Id = BlockWorld.GetBlock(blockPos);
        if (!BlockRegistry->BlocksMovement(Id))
        {
          continue;
        }
        const glm::vec3 blockCenter = BlockCenter(blockPos);
        if (UCube::CheckAabbCollision(center, half, blockCenter, blockHalf))
        {
          return true;
        }
      }
    }
  }
  return false;
}

bool UWorldCollision::MayContainSolid(const CollisionVolume &vol) const
{
  const glm::vec3 min_pos = vol.center - vol.halfExtents;
  const glm::vec3 max_pos = vol.center + vol.halfExtents;
  const glm::ivec3 min_cell = WorldPosToBlock(min_pos);
  const glm::ivec3 max_cell = WorldPosToBlock(max_pos);
  const int min_chunk_x = FloorDiv(std::min(min_cell.x, max_cell.x), CHUNK_SIZE);
  const int max_chunk_x = FloorDiv(std::max(min_cell.x, max_cell.x), CHUNK_SIZE);
  const int min_chunk_y = FloorDiv(std::min(min_cell.y, max_cell.y), CHUNK_SIZE);
  const int max_chunk_y = FloorDiv(std::max(min_cell.y, max_cell.y), CHUNK_SIZE);
  const int min_chunk_z = FloorDiv(std::min(min_cell.z, max_cell.z), CHUNK_SIZE);
  const int max_chunk_z = FloorDiv(std::max(min_cell.z, max_cell.z), CHUNK_SIZE);

  for (int cx = min_chunk_x; cx <= max_chunk_x; ++cx)
  {
    for (int cy = min_chunk_y; cy <= max_chunk_y; ++cy)
    {
      for (int cz = min_chunk_z; cz <= max_chunk_z; ++cz)
      {
        const glm::ivec3 chunk_coord(cx, cy, cz);
        const uint64_t mask = QueryChunkOccupancyMask(chunk_coord);
        if (mask == 0)
        {
          continue;
        }
        const glm::ivec3 chunk_origin(cx * CHUNK_SIZE, cy * CHUNK_SIZE,
                                      cz * CHUNK_SIZE);
        const glm::ivec3 local_min = glm::clamp(min_cell - chunk_origin,
                                                glm::ivec3(0), glm::ivec3(CHUNK_SIZE - 1));
        const glm::ivec3 local_max = glm::clamp(max_cell - chunk_origin,
                                                glm::ivec3(0), glm::ivec3(CHUNK_SIZE - 1));
        const int min_sx = local_min.x / kSubchunkSize;
        const int max_sx = local_max.x / kSubchunkSize;
        const int min_sy = local_min.y / kSubchunkSize;
        const int max_sy = local_max.y / kSubchunkSize;
        const int min_sz = local_min.z / kSubchunkSize;
        const int max_sz = local_max.z / kSubchunkSize;
        for (int sx = min_sx; sx <= max_sx; ++sx)
        {
          for (int sy = min_sy; sy <= max_sy; ++sy)
          {
            for (int sz = min_sz; sz <= max_sz; ++sz)
            {
              const int bit = sx + sy * SubchunksPerAxis +
                              sz * SubchunksPerAxis * SubchunksPerAxis;
              if (MaskHasSubchunk(mask, bit))
              {
                return true;
              }
            }
          }
        }
      }
    }
  }
  return false;
}

void UWorldCollision::InvalidateChunkMovementSolid(glm::ivec3 chunk_coord)
{
  ChunkOccupancyMask.erase(chunk_coord);
}

void UWorldCollision::RemoveChunkMovementSolidCache(glm::ivec3 chunk_coord)
{
  ChunkOccupancyMask.erase(chunk_coord);
}

void UWorldCollision::RebuildChunkMovementSolid(glm::ivec3 chunk_coord)
{
  if (!BlockRegistry)
  {
    ChunkOccupancyMask.erase(chunk_coord);
    return;
  }
  const UChunk *chunk = BlockWorld.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    ChunkOccupancyMask.erase(chunk_coord);
    return;
  }
  uint64_t mask = 0;
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int ly = 0; ly < CHUNK_SIZE; ++ly)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        const glm::ivec3 block_pos = origin + glm::ivec3(lx, ly, lz);
        const BlockId id = BlockWorld.GetBlock(block_pos);
        if (!BlockRegistry->BlocksMovement(id))
        {
          continue;
        }
        const int bit = SubchunkBitIndex(glm::ivec3(lx, ly, lz));
        mask |= (1ull << bit);
      }
    }
  }
  ChunkOccupancyMask[chunk_coord] = mask;
}

bool UWorldCollision::QueryChunkMovementSolid(glm::ivec3 chunk_coord) const
{
  return QueryChunkOccupancyMask(chunk_coord) != 0;
}

uint64_t UWorldCollision::QueryChunkOccupancyMask(glm::ivec3 chunk_coord) const
{
  if (const auto it = ChunkOccupancyMask.find(chunk_coord);
      it != ChunkOccupancyMask.end())
  {
    return it->second;
  }
  if (!BlockRegistry)
  {
    return 0;
  }
  const UChunk *chunk = BlockWorld.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return 0;
  }
  if (Telemetry)
  {
    ++Telemetry->CollisionBroadphaseFallbacks;
  }
  uint64_t mask = 0;
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  int index = 0;
  for (const BlockId block_id : chunk->GetData())
  {
    if (BlockRegistry->BlocksMovement(block_id))
    {
      const int lx = index % CHUNK_SIZE;
      const int ly = (index / CHUNK_SIZE) % CHUNK_SIZE;
      const int lz = index / (CHUNK_SIZE * CHUNK_SIZE);
      const int bit = SubchunkBitIndex(glm::ivec3(lx, ly, lz));
      mask |= (1ull << bit);
    }
    ++index;
  }
  ChunkOccupancyMask[chunk_coord] = mask;
  return mask;
}

bool UWorldCollision::CheckCreatureCollisionVolume(
    const CollisionVolume &vol, CreatureId skipCreatureId) const
{
#if defined(CUTUM_PHYSICS_LIGHT_REGISTRY)
  (void)vol;
  (void)skipCreatureId;
  return false;
#else
  if (!Environment)
  {
    return false;
  }
  return Environment->CheckCreatureCollisionVolume(vol, skipCreatureId);
#endif
}

bool UWorldCollision::CheckCollisionVolume(const CollisionVolume &vol,
                                           CreatureId skipCreatureId) const
{
  if (CheckBlockCollisionVolume(vol))
  {
    return true;
  }
  if (!EntityCollisionEnabled)
  {
    return false;
  }
  return CheckCreatureCollisionVolume(vol, skipCreatureId);
}

bool UWorldCollision::CheckCollision(const glm::vec3 &eyePos,
                                     const PlayerCapsule &cap,
                                     CreatureId skipCreatureId) const
{
  return CheckCollisionVolume(CollisionVolumeFromEye(eyePos, cap),
                              skipCreatureId);
}

bool UWorldCollision::DepenetrateEye(glm::vec3 &eyePos,
                                     const PlayerCapsule &cap,
                                     CreatureId skipCreatureId) const
{
  constexpr int kMaxIterations = 32;
  constexpr float kStep = 0.05f;
  if (!CheckCollision(eyePos, cap, skipCreatureId))
  {
    return true;
  }
  for (int i = 0; i < kMaxIterations; ++i)
  {
    eyePos.y += kStep;
    if (!CheckCollision(eyePos, cap, skipCreatureId))
    {
      return true;
    }
  }
  return !CheckCollision(eyePos, cap, skipCreatureId);
}

bool UWorldCollision::HasGroundSupportVolume(const CollisionVolume &vol,
                                             float feetY) const
{
  if (!BlockRegistry)
  {
    return false;
  }
  const FootprintStandSampleStats stats = SampleFootprintAtFeet(
      *this, BlockWorld, *BlockRegistry, feetY, vol.center.x, vol.center.z,
      vol.halfExtents.x, PlayerCapsule{}, false);
  int minSamples = kFootprintMinSolidSamples;
  if (vol.halfExtents.x < 0.30f)
  {
    minSamples = 1;
  }
  else if (vol.halfExtents.x < 0.45f)
  {
    minSamples = 2;
  }
  return stats.centerSolid && stats.solidSamples >= minSamples;
}

bool UWorldCollision::HasGroundSupport(const glm::vec3 &eyePos,
                                        const PlayerCapsule &cap) const
{
  return HasGroundSupportVolume(CollisionVolumeFromEye(eyePos, cap),
                                cap.feetY(eyePos));
}

glm::vec3 UWorldCollision::ResolveMovementBody(const glm::vec3 &bodyOrigin,
                                               const glm::vec3 &delta,
                                               const glm::vec3 &currentSizeBlocks,
                                               CreatureId skipCreatureId) const
{
  if (glm::dot(delta, delta) < 1e-10f)
  {
    return bodyOrigin;
  }
  glm::vec3 body = bodyOrigin;
  body = ResolveMovementAxisBody(*this, body, delta.y, 1, currentSizeBlocks,
                                 skipCreatureId);
  body = ResolveMovementAxisBody(*this, body, delta.x, 0, currentSizeBlocks,
                                 skipCreatureId);
  body = ResolveMovementAxisBody(*this, body, delta.z, 2, currentSizeBlocks,
                                 skipCreatureId);
  return body;
}

glm::vec3 UWorldCollision::ResolveMovement(const glm::vec3 &eyePos,
                                           const glm::vec3 &delta,
                                           const PlayerCapsule &cap,
                                           CreatureId skipCreatureId) const
{
  if (glm::dot(delta, delta) < 1e-10f)
  {
    return eyePos;
  }
  glm::vec3 resolvedEye = eyePos;
  if (delta.y > 0.0f && CheckCollision(resolvedEye, cap, skipCreatureId))
  {
    DepenetrateEye(resolvedEye, cap, skipCreatureId);
  }
  const glm::vec3 eyeOffset(0.0f, cap.eyeHeight, 0.0f);
  const glm::vec3 sizeBlocks(cap.halfWidth * 2.0f, cap.height,
                             cap.halfWidth * 2.0f);
  const glm::vec3 body = BodyOriginFromEye(resolvedEye, eyeOffset);
  const glm::vec3 newBody =
      ResolveMovementBody(body, delta, sizeBlocks, skipCreatureId);
  return BoundsEyePosition(newBody, eyeOffset);
}

UWorldCollision::StepUpProbe
UWorldCollision::ProbeStepUp(const glm::vec3 &eyePos, const glm::vec3 &horiz,
                             const PlayerCapsule &cap,
                             float maxTriggerDistance) const
{
  StepUpProbe probe{};
  if (!BlockRegistry)
  {
    return probe;
  }
  const glm::vec3 horizFlat(horiz.x, 0.0f, horiz.z);
  const float horizLen = glm::length(horizFlat);
  if (horizLen < 1e-6f)
  {
    return probe;
  }
  const glm::vec3 dir = horizFlat / horizLen;
  glm::ivec3 stepCell(0);
  if (!FindSteppableLedge(*this, BlockWorld, *BlockRegistry, eyePos, dir, cap,
                          Environment ? Environment->GetControlledCreatureId() : 0,
                          stepCell))
  {
    return probe;
  }
  const float dist = DistanceToStepRiser(eyePos, stepCell, dir, cap);
  if (dist < -0.02f || dist > maxTriggerDistance)
  {
    return probe;
  }
  probe.Valid = true;
  probe.DistanceToLedge = dist;
  probe.MoveDir = dir;
  probe.TargetPos = StepStandPosition(stepCell, cap);
  return probe;
}

bool UWorldCollision::GetStepUpLanding(const glm::vec3 &eyePos,
                                       const glm::vec3 &horiz,
                                       const PlayerCapsule &cap,
                                       float maxTriggerDistance,
                                       glm::vec3 &outLanding) const
{
  const StepUpProbe probe = ProbeStepUp(eyePos, horiz, cap, maxTriggerDistance);
  if (!probe.Valid)
  {
    return false;
  }
  glm::ivec3 stepCell(0);
  const glm::vec3 horizFlat(horiz.x, 0.0f, horiz.z);
  const float horizLen = glm::length(horizFlat);
  if (horizLen < 1e-6f)
  {
    return false;
  }
  const glm::vec3 dir = horizFlat / horizLen;
  if (!FindSteppableLedge(*this, BlockWorld, *BlockRegistry, eyePos, dir, cap,
                          Environment ? Environment->GetControlledCreatureId() : 0,
                          stepCell))
  {
    return false;
  }
  const glm::ivec3 feetCell =
      WorldPosToBlock(glm::vec3(eyePos.x, cap.feetY(eyePos) + 0.01f, eyePos.z));
  if (feetCell.x == stepCell.x && feetCell.z == stepCell.z &&
      feetCell.y >= stepCell.y)
  {
    return false;
  }

  outLanding = probe.TargetPos - glm::vec3(probe.MoveDir.x * 0.18f, 0.0f,
                                           probe.MoveDir.z * 0.18f);
  return !CheckCollision(outLanding, cap,
                         Environment ? Environment->GetControlledCreatureId() : 0);
}

bool UWorldCollision::TryStepUp(glm::vec3 &eyePos, const glm::vec3 &horiz,
                                const PlayerCapsule &cap,
                                float maxTriggerDistance) const
{
  glm::vec3 landing = eyePos;
  if (!GetStepUpLanding(eyePos, horiz, cap, maxTriggerDistance, landing))
  {
    return false;
  }
  eyePos = landing;
  return true;
}

bool UWorldCollision::CheckPositionFree(const glm::vec3 &position,
                                        float /*size*/) const
{
  return BlockWorld.IsAir(WorldPosToBlock(position));
}

std::optional<glm::vec3> UWorldCollision::FindNearestFreeCubePosition(
    const glm::vec3 &position, const glm::vec3 &front,
    const PlayerCapsule &cap) const
{
  if (!BlockRegistry)
  {
    return std::nullopt;
  }
  const auto hit =
      RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front);
  if (!hit)
  {
    return std::nullopt;
  }

  glm::ivec3 normal = hit->faceNormal;
  if (normal == glm::ivec3(0))
  {
    const glm::vec3 toCamera = position - BlockCenter(hit->blockPos);
    if (std::abs(toCamera.x) >= std::abs(toCamera.y) &&
        std::abs(toCamera.x) >= std::abs(toCamera.z))
    {
      normal.x = toCamera.x > 0.0f ? 1 : -1;
    }
    else if (std::abs(toCamera.y) >= std::abs(toCamera.z))
    {
      normal.y = toCamera.y > 0.0f ? 1 : -1;
    }
    else
    {
      normal.z = toCamera.z > 0.0f ? 1 : -1;
    }
  }

  const glm::ivec3 placePos = hit->blockPos + normal;
  if (!BlockWorld.IsAir(placePos))
  {
    return std::nullopt;
  }

  const glm::vec3 res_position = BlockCenter(placePos);
  if (!CheckPositionFree(res_position, 1.0f))
  {
    return std::nullopt;
  }

  const glm::ivec3 feet_block = WorldPosToBlock(position);
  const bool placing_into_pit =
      placePos.y < feet_block.y &&
      std::abs(placePos.x - feet_block.x) <= 1 &&
      std::abs(placePos.z - feet_block.z) <= 1;

  if (!placing_into_pit)
  {
    const CollisionVolume vol = CollisionVolumeFromEye(position, cap);
    const glm::vec3 blockCenter = BlockCenter(placePos);
    const glm::vec3 blockHalf(0.5f);
    if (UCube::CheckAabbCollision(vol.center, vol.halfExtents, blockCenter,
                                  blockHalf))
    {
      return std::nullopt;
    }
  }

  return res_position;
}

} // namespace cutum
