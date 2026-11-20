#include "Blocks/BlockRegistry.h"
#include "Core/FrameDeadline.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Chunks/ChunkStreamer.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/RuntimeTuning.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/Streaming/StreamerAmortizePolicy.h"
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

static void TestRetiredUnloadCandidate()
{
  using namespace cutum;
  UBlockWorld world;
  UBlockRegistry registry(nullptr);
  // The cursor snapshots both columns; the second can retire before its turn.
  const glm::ivec3 first(40, -4, 3), second(41, -4, 3);
  world.SetBlock(first * CHUNK_SIZE, 8);
  world.SetBlock(second * CHUNK_SIZE, 8);
  UChunkStreamer streamer(world, registry, 1, 0, 319, -64);
  streamer.SetMaxUnloadOpsPerFrame(1);
  int saved = 0, unloaded = 0;
  streamer.SetCallbacks(
      nullptr, [&](glm::ivec3) { ++saved; }, nullptr, nullptr);
  streamer.SetUnloadColumnCallback(
      [&](glm::ivec3, int)
      {
        ++unloaded;
        return true;
      });
  auto &tuning = URuntimeTuning::Get();
  const int prior = tuning.UnloadAmortizeMode;
  tuning.UnloadAmortizeMode = kUnloadAmortizeUB;
  UFrameDeadline::Get().BeginFrame(0);
  const PlayerCapsule cap;
  const glm::vec3 eye(8, -60, 8);
  streamer.UnloadPass({0, -4, 0}, eye, cap);
  Expect(saved == 1 && unloaded == 1 &&
             !world.GetChunkManager().HasChunk(first),
         "first resident candidate saves once before eviction");
  world.GetChunkManager().RemoveChunk(second);
  streamer.UnloadPass({0, -4, 0}, eye, cap);
  Expect(
      saved == 1 && unloaded == 1,
      "retired cursor candidate cannot save or purge a column with no terrain");
  // Geometry outside the configured vertical domain cannot authorize saving
  // an absent terrain column. It must not repeatedly delete its disk snapshot.
  world.SetBlock({second.x * CHUNK_SIZE, -81, second.z * CHUNK_SIZE}, 8);
  streamer.UnloadPass({0, -4, 0}, eye, cap);
  streamer.UnloadPass({0, -4, 0}, eye, cap);
  Expect(saved == 1 && unloaded == 1,
         "out-of-profile geometry cannot authorize an empty terrain eviction");
  world.SetBlock(second * CHUNK_SIZE, 50);
  streamer.UnloadPass({0, -4, 0}, eye, cap);
  Expect(saved == 2 && unloaded == 2 &&
             !world.GetChunkManager().HasChunk(second),
         "returned resident geometry has a fresh save obligation");
  tuning.UnloadAmortizeMode = prior;
}

static void TestAirRetiresWithTerrainColumn()
{
  using namespace cutum;
  auto &tuning = URuntimeTuning::Get();
  const int prior = tuning.UnloadAmortizeMode;
  for (int mode : {kUnloadAmortizeU0, kUnloadAmortizeUB, kUnloadAmortizeUD})
  {
    tuning.UnloadAmortizeMode = mode;
    UBlockWorld world;
    UBlockRegistry registry(nullptr);
    const glm::ivec3 floor(40, -4, 3), air_coord(40, -3, 3);
    for (int x = 0; x < CHUNK_SIZE; ++x)
      for (int z = 0; z < CHUNK_SIZE; ++z)
        world.SetBlock(floor * CHUNK_SIZE + glm::ivec3(x, 0, z), 8);
    UChunk legacy_air(air_coord);
    std::array<uint8_t, CHUNK_VOLUME> pattern;
    pattern.fill(0x53);
    legacy_air.ReplaceLightData(pattern);
    legacy_air.SetWeatherState(.25f, 123.125);
    UBinaryChunkSerializer serializer;
    const auto encoded = serializer.Serialize(air_coord, legacy_air, registry);
    const auto decoded =
        serializer.Deserialize(encoded.bytes, air_coord, registry);
    const auto air = decoded.GetDecodedAirSlice(air_coord);
    auto &manager = world.GetChunkManager();
    Expect(air &&
               manager.InstallLoadedAirSlice(*air, {{40, 0, 3}, 0}, 0, false),
           "real decoded AIR belongs to the terrain column before unload");
    const auto old_incarnation =
        manager.GetSectionLightField(air_coord)->GetIncarnation();
    UChunkStreamer streamer(world, registry, 1, 0, 319, -64);
    streamer.SetMaxUnloadOpsPerFrame(1);
    int saved = 0;
    streamer.SetCallbacks(
        nullptr,
        [&](glm::ivec3)
        {
          auto snapshot = manager.MakeTerrainSaveSnapshot(air_coord);
          Expect(snapshot && snapshot->IsAirOnly() &&
                     snapshot->GetLightData() == pattern &&
                     snapshot->GetWeatherLastUpdateSec() == 123.125,
                 "unload save observes AIR metadata before retiring its "
                 "authority");
          ++saved;
        },
        nullptr, nullptr);
    streamer.SetUnloadColumnCallback([](glm::ivec3, int) { return true; });
    UFrameDeadline::Get().BeginFrame(0);
    streamer.UnloadPass({0, -4, 0}, {8, -60, 8}, PlayerCapsule{});
    Expect(saved == 1 && !manager.HasChunk(floor),
           "real terrain column was saved and its geometry evicted");
    Expect(!manager.HasLoadedAirSlice(air_coord) &&
               !manager.GetSectionLightField(air_coord) &&
               manager.QueryBlock(air_coord * CHUNK_SIZE).IsUnloaded(),
           "streamer eviction must retire loaded AIR data and field ownership "
           "too");
    Expect(manager.InstallLoadedAirSlice(*air, {{40, 0, 3}, 1}, 1, false) &&
               manager.GetSectionLightField(air_coord)->GetIncarnation() !=
                   old_incarnation &&
               !manager.HasChunk(air_coord),
           "returned AIR starts a fresh field without a geometry holder");
  }
  tuning.UnloadAmortizeMode = prior;
}

int main()
{
  TestAirRetiresWithTerrainColumn();
  TestRetiredUnloadCandidate();
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
