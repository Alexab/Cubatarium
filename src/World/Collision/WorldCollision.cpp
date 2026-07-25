#include "World/Collision/WorldCollision.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Render/Primitives/Cube.h"
#include "World/Chunks/Chunk.h"
#include "World/Collision/VoxelDdaTraversal.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Environment/WorldEnvironment.h"
#include "World/Math/FluidCellState.h"
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

FootprintStandSampleStats SampleFootprintAtFeet(
    const UWorldCollision &collision, const UBlockWorld &blockWorld,
    const UBlockRegistry &registry, float feetY, float centerX, float centerZ,
    float halfWidth, const PlayerCapsule &cap, bool checkValidStand)
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
  TraverseVoxelRay(
      rayOrigin, axisUnit * sign, remaining,
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
  // Axis-aligned step only: pick the dominant horizontal axis so corner
  // approaches do not diagonal-jump onto the adjacent block.
  int dx = 0;
  int dz = 0;
  const float ax = std::abs(dir.x);
  const float az = std::abs(dir.z);
  if (ax >= az)
  {
    if (ax > 0.25f)
    {
      dx = dir.x > 0.0f ? 1 : -1;
    }
  }
  else if (az > 0.25f)
  {
    dz = dir.z > 0.0f ? 1 : -1;
  }
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
  // IsValidStandCell already verified the stand volume via CollisionVolumeAtFeet.
  // Do not re-check through eye→feet (BlockTopY+eyeHeight-eyeHeight): float
  // round-trip can sit slightly inside the floor and false-reject the ledge.
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

std::optional<float>
UWorldCollision::QueryGroundFeetYUnder(int worldX, int worldZ,
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
  const float feetY = BlockTopY(cell.y);
  const glm::vec3 sizeBlocks = SizeBlocksFromCapsule(cap);
  const CollisionVolume vol =
      CollisionVolumeAtFeet(feetY, static_cast<float>(cell.x),
                            static_cast<float>(cell.z), sizeBlocks);
  return !CheckBlockCollisionVolume(vol);
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

bool UWorldCollision::CheckBlockCollisionVolume(
    const CollisionVolume &vol) const
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
  const int min_chunk_x =
      FloorDiv(std::min(min_cell.x, max_cell.x), CHUNK_SIZE);
  const int max_chunk_x =
      FloorDiv(std::max(min_cell.x, max_cell.x), CHUNK_SIZE);
  const int min_chunk_y =
      FloorDiv(std::min(min_cell.y, max_cell.y), CHUNK_SIZE);
  const int max_chunk_y =
      FloorDiv(std::max(min_cell.y, max_cell.y), CHUNK_SIZE);
  const int min_chunk_z =
      FloorDiv(std::min(min_cell.z, max_cell.z), CHUNK_SIZE);
  const int max_chunk_z =
      FloorDiv(std::max(min_cell.z, max_cell.z), CHUNK_SIZE);

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
        const glm::ivec3 local_min = glm::clamp(
            min_cell - chunk_origin, glm::ivec3(0), glm::ivec3(CHUNK_SIZE - 1));
        const glm::ivec3 local_max = glm::clamp(
            max_cell - chunk_origin, glm::ivec3(0), glm::ivec3(CHUNK_SIZE - 1));
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
  const glm::vec3 sizeBlocks = SizeBlocksFromCapsule(cap);
  glm::vec3 body(eyePos.x, cap.feetY(eyePos), eyePos.z);
  if (!CheckCollisionVolume(CollisionVolumeFromBody(body, sizeBlocks),
                            skipCreatureId))
  {
    return true;
  }
  for (int i = 0; i < kMaxIterations; ++i)
  {
    body.y += kStep;
    if (!CheckCollisionVolume(CollisionVolumeFromBody(body, sizeBlocks),
                              skipCreatureId))
    {
      eyePos.y = body.y + cap.eyeHeight;
      return true;
    }
  }
  eyePos.y = body.y + cap.eyeHeight;
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

bool UWorldCollision::DepenetrateCreatureBodyXZ(glm::vec3 &bodyOrigin,
                                                const glm::vec3 &sizeBlocks,
                                                CreatureId skipCreatureId) const
{
  if (!EntityCollisionEnabled || !Environment)
  {
    return true;
  }
  CollisionVolume self = CollisionVolumeFromBody(bodyOrigin, sizeBlocks);
  if (!CheckCreatureCollisionVolume(self, skipCreatureId))
  {
    return true;
  }

  const float query_radius =
      std::max(sizeBlocks.x, sizeBlocks.z) + 1.25f;
  const std::vector<CreatureNeighborView> neighbors =
      Environment->QueryCreatureNeighborsInRadius(bodyOrigin, query_radius,
                                                  skipCreatureId);
  glm::vec3 push_sum(0.0f);
  int hits = 0;
  for (const CreatureNeighborView &neighbor : neighbors)
  {
    const UCreature *other = Environment->GetCreature(neighbor.Id);
    if (!other || other->IsPlayerCharacter())
    {
      continue;
    }
    const CollisionVolume other_vol = other->GetCollisionVolume();
    if (!UCube::CheckAabbCollision(self.center, self.halfExtents,
                                   other_vol.center, other_vol.halfExtents))
    {
      continue;
    }
    const float overlap_x = self.halfExtents.x + other_vol.halfExtents.x -
                            std::abs(self.center.x - other_vol.center.x);
    const float overlap_z = self.halfExtents.z + other_vol.halfExtents.z -
                            std::abs(self.center.z - other_vol.center.z);
    if (overlap_x <= 0.0f || overlap_z <= 0.0f)
    {
      continue;
    }
    glm::vec3 delta = self.center - other_vol.center;
    delta.y = 0.0f;
    float dist = glm::length(delta);
    if (dist < 1e-4f)
    {
      const float ang =
          static_cast<float>((skipCreatureId * 17 + neighbor.Id * 31) % 628) /
          100.0f;
      delta = glm::vec3(std::cos(ang), 0.0f, std::sin(ang));
      dist = 1.0f;
    }
    else
    {
      delta /= dist;
    }
    const float push_amt = std::min(overlap_x, overlap_z) + 0.04f;
    push_sum += delta * push_amt;
    ++hits;
  }
  if (hits == 0)
  {
    return false;
  }

  float push_len = glm::length(push_sum);
  if (push_len < 1e-4f)
  {
    return false;
  }
  // Cap per call so we don't teleport through walls.
  constexpr float kMaxPush = 0.45f;
  if (push_len > kMaxPush)
  {
    push_sum *= kMaxPush / push_len;
    push_len = kMaxPush;
  }

  const glm::vec3 dir = push_sum / push_len;
  constexpr float kDirs[] = {1.0f, 0.7f, 0.4f, 0.2f};
  for (float scale : kDirs)
  {
    glm::vec3 candidate = bodyOrigin + dir * (push_len * scale);
    const CollisionVolume cand =
        CollisionVolumeFromBody(candidate, sizeBlocks);
    if (CheckBlockCollisionVolume(cand))
    {
      continue;
    }
    bodyOrigin.x = candidate.x;
    bodyOrigin.z = candidate.z;
    self = CollisionVolumeFromBody(bodyOrigin, sizeBlocks);
    if (!CheckCreatureCollisionVolume(self, skipCreatureId))
    {
      return true;
    }
  }

  // Still overlapping: keep best block-clear push for next frame.
  glm::vec3 candidate = bodyOrigin + dir * std::min(push_len, 0.2f);
  if (!CheckBlockCollisionVolume(
          CollisionVolumeFromBody(candidate, sizeBlocks)))
  {
    bodyOrigin.x = candidate.x;
    bodyOrigin.z = candidate.z;
  }
  return !CheckCreatureCollisionVolume(
      CollisionVolumeFromBody(bodyOrigin, sizeBlocks), skipCreatureId);
}

glm::vec3 UWorldCollision::ResolveMovementBody(
    const glm::vec3 &bodyOrigin, const glm::vec3 &delta,
    const glm::vec3 &currentSizeBlocks, CreatureId skipCreatureId) const
{
  glm::vec3 body = bodyOrigin;
  if (EntityCollisionEnabled)
  {
    DepenetrateCreatureBodyXZ(body, currentSizeBlocks, skipCreatureId);
  }
  if (glm::dot(delta, delta) < 1e-10f)
  {
    return body;
  }
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
  const glm::vec3 sizeBlocks = SizeBlocksFromCapsule(cap);
  const float feetY = cap.feetY(eyePos);
  glm::vec3 body(eyePos.x, feetY, eyePos.z);

  if (EntityCollisionEnabled)
  {
    DepenetrateCreatureBodyXZ(body, sizeBlocks, skipCreatureId);
  }

  if (glm::dot(delta, delta) < 1e-10f)
  {
    return glm::vec3(body.x, body.y + cap.eyeHeight, body.z);
  }

  if (delta.y > 0.0f &&
      CheckCollisionVolume(CollisionVolumeFromBody(body, sizeBlocks),
                           skipCreatureId))
  {
    glm::vec3 resolvedEye(body.x, body.y + cap.eyeHeight, body.z);
    DepenetrateEye(resolvedEye, cap, skipCreatureId);
    body.y = cap.feetY(resolvedEye);
    body.x = resolvedEye.x;
    body.z = resolvedEye.z;
  }

  const glm::vec3 newBody =
      ResolveMovementBody(body, delta, sizeBlocks, skipCreatureId);
  return glm::vec3(newBody.x, newBody.y + cap.eyeHeight, newBody.z);
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
  if (!FindSteppableLedge(
          *this, BlockWorld, *BlockRegistry, eyePos, dir, cap,
          Environment ? Environment->GetControlledCreatureId() : 0, stepCell))
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
  if (!FindSteppableLedge(
          *this, BlockWorld, *BlockRegistry, eyePos, dir, cap,
          Environment ? Environment->GetControlledCreatureId() : 0, stepCell))
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

  outLanding = probe.TargetPos;
  return true;
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
  return IsPlaceableForSolidBlock(WorldPosToBlock(position));
}

bool UWorldCollision::IsPlaceableForSolidBlock(glm::ivec3 pos) const
{
  const BlockId id = BlockWorld.GetBlock(pos);
  if (id == BLOCK_AIR)
  {
    return true;
  }
  if (!BlockRegistry)
  {
    return false;
  }
  if (BlockRegistry->IsLiquid(id))
  {
    return true;
  }
  // Worldgen scatter / cross plants (occupancy 0) sit in carved pits and must
  // be replaceable like air for hotbar solids.
  return !BlockRegistry->BlocksMovement(id);
}

bool UWorldCollision::CanPlaceClassic(glm::ivec3 place_pos,
                                      const glm::vec3 &eye,
                                      const glm::vec3 &front,
                                      const PlayerCapsule &cap,
                                      float max_distance) const
{
  if (!IsPlaceableForSolidBlock(place_pos))
  {
    return false;
  }
  const glm::vec3 res_position = BlockCenter(place_pos);
  if (!CheckPositionFree(res_position, 1.0f))
  {
    return false;
  }

  const glm::ivec3 feet_block =
      WorldPosToBlock(glm::vec3(eye.x, cap.feetY(eye) + 0.01f, eye.z));
  const int xz_radius = place_pos.y <= feet_block.y
                            ? std::max(4, static_cast<int>(max_distance))
                            : 1;
  const bool placing_into_pit =
      place_pos.y <= feet_block.y &&
      std::abs(place_pos.x - feet_block.x) <= xz_radius &&
      std::abs(place_pos.z - feet_block.z) <= xz_radius;

  if (!placing_into_pit)
  {
    const CollisionVolume vol = CollisionVolumeFromEye(eye, cap);
    const glm::vec3 blockCenter = BlockCenter(place_pos);
    const glm::vec3 blockHalf(0.5f);
    if (UCube::CheckAabbCollision(vol.center, vol.halfExtents, blockCenter,
                                  blockHalf))
    {
      return false;
    }
  }

  return true;
}

BlockPlacementResolve UWorldCollision::ResolveBlockPlacement(
    const glm::vec3 &eye, const glm::vec3 &front, const PlayerCapsule &cap,
    float max_distance) const
{
  return ResolveBlockPlacement(eye, front, cap, max_distance, eye);
}

BlockPlacementResolve UWorldCollision::ResolveBlockPlacement(
    const glm::vec3 &ray_origin, const glm::vec3 &front, const PlayerCapsule &cap,
    float max_distance, const glm::vec3 &player_eye) const
{
  BlockPlacementResolve result;
  if (!BlockRegistry)
  {
    return result;
  }

  constexpr float kRaycastDistance = 128.0f;

  const auto hit = RaycastSolidBlocks(BlockWorld, *BlockRegistry, ray_origin,
                                      front, kRaycastDistance);
  if (!hit)
  {
    return result;
  }

  const glm::ivec3 place_pos =
      hit->blockPos + InferPlacementNormal(*hit, ray_origin);
  const glm::ivec3 feet_block = WorldPosToBlock(
      glm::vec3(player_eye.x, cap.feetY(player_eye) + 0.01f, player_eye.z));
  const int pit_xz_radius = std::max(4, static_cast<int>(max_distance));
  const bool pit_placement =
      place_pos.y <= feet_block.y &&
      std::abs(place_pos.x - feet_block.x) <= pit_xz_radius &&
      std::abs(place_pos.z - feet_block.z) <= pit_xz_radius;
  if (!pit_placement && hit->distance > max_distance)
  {
    return result;
  }

  result.break_hit = hit;
  if (CanPlaceClassic(place_pos, player_eye, front, cap, max_distance))
  {
    result.place_block_pos = place_pos;
  }

  return result;
}

std::optional<glm::vec3>
UWorldCollision::FindNearestFreeCubePosition(const glm::vec3 &position,
                                             const glm::vec3 &front,
                                             const PlayerCapsule &cap) const
{
  const BlockPlacementResolve resolved =
      ResolveBlockPlacement(position, front, cap);
  if (!resolved.place_block_pos)
  {
    return std::nullopt;
  }
  return BlockCenter(*resolved.place_block_pos);
}

bool UWorldCollision::CheckRayIntersection(
    const glm::vec3 &position, const glm::vec3 &front,
    std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>
        &distance_map) const
{
  distance_map.clear();
  if (!BlockRegistry)
  {
    return false;
  }
  const auto hit =
      RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front);
  if (!hit)
  {
    return false;
  }
  const glm::vec3 hitCenter = BlockCenter(hit->blockPos);
  distance_map[hit->distance] =
      std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>(
          0, glm::vec3(hit->faceNormal), hitCenter, 0, 0);
  return true;
}

bool UWorldCollision::CheckRayIntersection(const glm::vec3 &position,
                                           const glm::vec3 &front,
                                           glm::vec3 &intersection,
                                           float &distance, size_t &cube_index,
                                           int &cube_side,
                                           size_t &object_index) const
{
  std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>
      distance_map;
  const bool result = CheckRayIntersection(position, front, distance_map);
  if (result)
  {
    cube_side = std::get<0>(distance_map.begin()->second);
    intersection = std::get<2>(distance_map.begin()->second);
    distance = distance_map.begin()->first;
    cube_index = 0;
    object_index = 0;
  }
  return result;
}

FluidColumnSurface UWorldCollision::FindFluidColumnSurfaceAt(int bx, int bz,
                                                             int hintY) const
{
  if (!BlockRegistry)
  {
    return FluidColumnSurface{};
  }
  return ::cutum::FindFluidColumnSurfaceAt(
      BlockWorld, *BlockRegistry, bx, bz, hintY,
      URuntimeTuning::Get().FluidSurfaceScanUp,
      URuntimeTuning::Get().FluidSurfaceScanDown);
}

FluidColumnSurface
UWorldCollision::FindFluidColumnSurfaceEye(const glm::vec3 &eye) const
{
  const int bx = WorldCoordToBlockIndex(eye.x);
  const int bz = WorldCoordToBlockIndex(eye.z);
  const int by = WorldCoordToBlockIndex(eye.y);
  return FindFluidColumnSurfaceAt(bx, bz, by);
}

SampledFluidState
UWorldCollision::SampleFluidPhysicsVolume(const CollisionVolume &vol) const
{
  SampledFluidState state;
  if (!BlockRegistry)
  {
    return state;
  }
  std::unordered_map<BlockId, int> fluidWeights;
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
        const BlockId id = BlockWorld.GetBlock(blockPos);
        const bool waterlogged =
            BlockRegistry->IsFluidPermeable(id) &&
            PackFluidCellState(BlockWorld.GetFluidState(blockPos)) != 0;
        if (id == BLOCK_AIR ||
            (BlockRegistry->BlocksMovement(id) && !waterlogged))
        {
          continue;
        }
        const glm::vec3 blockCenter = BlockCenter(blockPos);
        if (!AabbOverlap(center, half, blockCenter, blockHalf))
        {
          continue;
        }
        const BlockId physicsId =
            BlockRegistry->IsLiquid(id)
                ? id
                : (waterlogged ? BlockRegistry->GetIdByTypeName("water") : id);
        if (physicsId == BLOCK_AIR)
        {
          continue;
        }
        const auto &mov = BlockRegistry->Physics(physicsId).Movement;
        state.inFluid = true;
        fluidWeights[physicsId] += 1;
        state.DragHorizontal =
            std::max(state.DragHorizontal, mov.DragHorizontal);
        state.SinkSpeed = std::max(state.SinkSpeed, mov.SinkSpeed);
        state.RiseSpeed = std::max(state.RiseSpeed, mov.RiseSpeed);
      }
    }
  }
  if (state.inFluid)
  {
    state.blendWeight = 1.0f;
    int bestWeight = 0;
    for (const auto &entry : fluidWeights)
    {
      if (entry.second > bestWeight)
      {
        bestWeight = entry.second;
        state.dominantFluid = entry.first;
      }
    }
  }
  return state;
}

} // namespace cutum
