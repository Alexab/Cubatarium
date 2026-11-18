#include "Blocks/BlockRegistry.h"
#include "World/Chunks/ChunkStreamer.h"
#include "World/Core/BlockWorld.h"
#include <cstdlib>
#include <iostream>
#include <string>

namespace cutum
{
void CubatariumLogInfo(const char *, const std::string &) {}
} // namespace cutum

static void Expect(bool value, const char *message)
{
  if (!value)
  {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

int main()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr);
  const glm::ivec3 center(-21, 0, 3);
  for (int dz = -1; dz <= 1; ++dz)
    for (int dx = -1; dx <= 1; ++dx)
      for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
        for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
          world.SetBlock({(center.x + dx) * cutum::CHUNK_SIZE + x, -64,
                          (center.z + dz) * cutum::CHUNK_SIZE + z},
                         8);
  cutum::UChunkStreamer streamer(world, registry, 1, 0, 319, -64);
  int disk_requests = 0, async_requests = 0;
  streamer.SetCallbacks(
      [&](glm::ivec3, uint64_t)
      {
        ++disk_requests;
        return false;
      },
      nullptr, nullptr, nullptr);
  streamer.SetAsyncGeneration(true);
  streamer.SetAsyncCallbacks(
      [&](glm::ivec3, int, uint64_t) { ++async_requests; },
      [&](glm::ivec3 c) { return streamer.IsTerrainChunkCompleteCached(c); });
  for (int dz = -1; dz <= 1; ++dz)
    for (int dx = -1; dx <= 1; ++dx)
      streamer.NotifyChunkCommitted(center + glm::ivec3(dx, 0, dz));
  const glm::ivec3 feet(center.x * cutum::CHUNK_SIZE + 8, -60,
                        center.z * cutum::CHUNK_SIZE + 8);
  const auto resident_count = world.GetChunkManager().GetResidentChunkCount();
  for (int frame = 0; frame < 32; ++frame)
    streamer.EnsureCollisionChunks(feet);
  Expect(disk_requests == 0 && async_requests == 0,
         "repeated collision checks must not reload complete low columns");
  Expect(!world.GetChunkManager().HasChunk(center) &&
             world.GetChunkManager().GetResidentChunkCount() == resident_count,
         "ready checks must not materialize y=0 or sky geometry");

  world.SetBlock(
      {center.x * cutum::CHUNK_SIZE, -64, center.z * cutum::CHUNK_SIZE},
      cutum::BLOCK_AIR);
  streamer.NotifyChunkCommitted(center);
  streamer.EnsureCollisionChunks(feet);
  Expect(disk_requests == 0 && async_requests == 0,
         "digging a known floor must not reload over the player's edit");
  Expect(world.GetBlock({center.x * cutum::CHUNK_SIZE, -64,
                         center.z * cutum::CHUNK_SIZE}) == cutum::BLOCK_AIR,
         "collision readiness preserves the edited floor");
  disk_requests = async_requests = 0;
  world.GetChunkManager().RemoveChunk({center.x, -4, center.z});
  streamer.NotifyChunkCommitted(center);
  streamer.EnsureCollisionChunks(feet);
  Expect(disk_requests > 0 && async_requests > 0,
         "an evicted low column must request reload");
  cutum::UBlockWorld partial_world;
  partial_world.SetBlock(
      {center.x * cutum::CHUNK_SIZE, -64, center.z * cutum::CHUNK_SIZE}, 8);
  cutum::UChunkStreamer partial(partial_world, registry, 1, 0, 319, -64);
  disk_requests = async_requests = 0;
  partial.SetCallbacks(
      [&](glm::ivec3 c, uint64_t)
      {
        disk_requests += c == center ? 1 : 0;
        return false;
      },
      nullptr, nullptr, nullptr);
  partial.SetAsyncGeneration(true);
  partial.SetAsyncCallbacks(
      [&](glm::ivec3 c, int, uint64_t)
      { async_requests += c == center ? 1 : 0; },
      [&](glm::ivec3 c) { return partial.IsTerrainChunkCompleteCached(c); });
  partial.NotifyChunkCommitted(center);
  partial.EnsureCollisionChunks(feet);
  Expect(disk_requests > 0 && async_requests > 0,
         "a never-completed partial column still requests its missing data");
  std::cout << "chunk_streamer_committed_column_test: OK\n";
}
