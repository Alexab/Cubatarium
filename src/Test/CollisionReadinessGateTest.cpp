#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"
#include "World/Physics/CollisionReadiness.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "collision_readiness_gate_test: " << message << std::endl;
    std::exit(1);
  }
}

static void EnsureGroundChunk(cutum::UBlockWorld &world, glm::ivec3 ground_coord)
{
  if (world.GetChunkManager().HasChunk(ground_coord))
  {
    return;
  }
  const glm::ivec3 block(ground_coord.x * cutum::CHUNK_SIZE, 0,
                         ground_coord.z * cutum::CHUNK_SIZE);
  world.SetBlock(block, cutum::BLOCK_AIR);
}

int main()
{
  cutum::UBlockWorld world;
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      EnsureGroundChunk(world, glm::ivec3(dx, 0, dz));
    }
  }

  Expect(cutum::IsCollisionRingReady(world, glm::ivec3(8, 4, 8), 0),
         "radius 0 should be ready when feet chunk exists");
  Expect(cutum::IsCollisionRingReady(world, glm::ivec3(8, 4, 8), 1),
         "radius 1 should be ready when neighbor columns exist");
  Expect(!cutum::IsCollisionRingReady(world, glm::ivec3(8, 4, 8), 2),
         "radius 2 should fail when distant columns are missing");

  const glm::ivec3 negative_block(-1, 0, -1);
  const glm::ivec3 negative_chunk = cutum::UChunkManager::WorldToChunk(negative_block);
  Expect(negative_chunk.x <= -1 && negative_chunk.z <= -1,
         "negative world coords must map to negative chunk indices");

  EnsureGroundChunk(world, glm::ivec3(negative_chunk.x, 0, negative_chunk.z));
  Expect(cutum::IsCollisionRingReady(world, negative_block, 0),
         "negative chunk boundary should be ready after load");

  for (int dx = -2; dx <= 2; ++dx)
  {
    for (int dz = -2; dz <= 2; ++dz)
    {
      EnsureGroundChunk(world, glm::ivec3(dx, 0, dz));
    }
  }
  Expect(cutum::IsCollisionRingReady(world, glm::ivec3(8, 4, 8), 2),
         "radius 2 should be ready when full ring exists");

  cutum::UBlockWorld below_datum;
  const glm::ivec3 cave_point(-323, -60, 51);
  const auto cave_section = cutum::UChunkManager::WorldToChunk(cave_point);
  below_datum.GetChunkManager().EnsureChunk(cave_section);
  Expect(!below_datum.GetChunkManager().HasChunk(
             {cave_section.x, 0, cave_section.z}) &&
             cutum::IsCollisionRingReady(below_datum, cave_point, 0),
         "negative-height query uses its resident section, not y=0");
  Expect(!cutum::IsCollisionRingReady(below_datum, cave_point, 1),
         "unknown horizontal collision neighbors still block readiness");
  below_datum.GetChunkManager().EnsureChunk(
      {cave_section.x + 1, 0, cave_section.z});
  Expect(!cutum::IsCollisionRingReady(below_datum,
                                      cave_point + glm::ivec3(16, 0, 0), 0),
         "resident y=0 cannot certify missing geometry at the query height");
  below_datum.SetKnownAirColumnFloorQuery(
      [&](glm::ivec3 column) -> std::optional<int>
      {
        if (column.x == cave_section.x && column.z == cave_section.z)
          return -3;
        return std::nullopt;
      });
  const glm::ivec3 air_point(cave_point.x, -40, cave_point.z);
  const auto before = below_datum.GetChunkManager().GetResidentChunkCount();
  Expect(cutum::IsCollisionRingReady(below_datum, air_point, 0),
         "proven geometry-absent AIR permits point collision queries");
  Expect(!cutum::IsCollisionRingReady(below_datum, air_point, 1),
         "one proven AIR column does not certify unknown neighbors");
  below_datum.SetKnownAirColumnFloorQuery({});
  Expect(!cutum::IsCollisionRingReady(below_datum, air_point, 0),
         "withdrawn AIR evidence is observed without stale readiness cache");
  Expect(below_datum.GetChunkManager().GetResidentChunkCount() == before,
         "collision readiness never creates geometry or light resources");
  below_datum.GetChunkManager().RemoveChunk(cave_section);
  Expect(!cutum::IsCollisionRingReady(below_datum, cave_point, 0),
         "evicted negative-height geometry becomes unready");

  std::cout << "collision_readiness_gate_test: OK" << std::endl;
  return 0;
}
