#include "World/Lighting/RelightResultInstall.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/GpuSkylightColumnSeed.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << "relight_result_install_test: " << message << '\n';
    std::exit(1);
  }
}

void TestDomainsAndRevision()
{
  using namespace cutum;
  UChunk chunk({0, 0, 0});
  const glm::ivec3 cell(1, 2,
                        3); // Non-symmetric Y/Z catches packed-layout swaps.
  chunk.SetLightLocal(cell, 3, 7);
  std::array<uint8_t, CHUNK_VOLUME> computed{};
  const auto index = static_cast<size_t>(UChunk::LocalIndex(cell));
  computed[index] = PackLight(12, 9);
  auto revision = chunk.GetLightFieldRevision();
  Expect(InstallComputedLight(chunk, computed, true, false), "sky changes");
  Expect(chunk.GetSkyLightLocal(cell) == 12 &&
             chunk.GetBlockLightLocal(cell) == 7,
         "sky-only install preserves block light and local indexing");
  Expect(chunk.GetLightFieldRevision() == revision + 1,
         "one revision per install");
  revision = chunk.GetLightFieldRevision();
  Expect(!InstallComputedLight(chunk, computed, true, false),
         "same sky is a no-op");
  Expect(!InstallComputedLight(chunk, computed, false, false),
         "no domains is a no-op");
  Expect(chunk.GetLightFieldRevision() == revision, "no-op retains revision");
  computed[index] = PackLight(0, 9);
  Expect(InstallComputedLight(chunk, computed, false, true),
         "block light changes");
  Expect(chunk.GetSkyLightLocal(cell) == 12 &&
             chunk.GetBlockLightLocal(cell) == 9,
         "block-only install preserves sky");
  Expect(InstallComputedLight(chunk, computed, true, true),
         "both domains install");
  Expect(chunk.GetLightData() == computed,
         "full result installs byte-for-byte");
}

void TestLateralSkylightSurvivesInstall()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;
  // Small roof leaves open sky around it; horizontal flood lights its
  // underside.
  for (int x = 5; x <= 9; ++x)
    for (int z = 5; z <= 9; ++z)
      world.SetBlock({x, 10, z}, 8);
  RelightJobSpec spec;
  spec.block_positions.push_back({7, 9, 7});
  spec.min_world_y = 0;
  spec.max_world_y = CHUNK_SIZE - 1;
  spec.column_center_only = true;
  spec.job_id = 1;
  auto snapshot = UChunkRelightSnapshot::Capture(world, spec);
  const auto result = snapshot.Compute(registry);
  Expect(result.chunks.size() == 1, "one primary chunk computed");
  const glm::ivec3 shaded(7, 9, 7);
  const auto index = static_cast<size_t>(UChunk::LocalIndex(shaded));
  const int expected = UnpackSky(result.chunks.front().light_packed[index]);
  Expect(expected > 0 && expected < 15,
         "worker computes lateral sky under roof");

  // Reproduce the superseded apply algorithm: vertical seeding erases that sky.
  std::array<uint8_t, CHUNK_VOLUME> occupancy{}, vertical_sky{};
  for (int x = 5; x <= 9; ++x)
    for (int z = 5; z <= 9; ++z)
      occupancy[static_cast<size_t>((10 * CHUNK_SIZE + z) * CHUNK_SIZE + x)] =
          1;
  SeedSkylightColumnsCpu(occupancy, vertical_sky);
  Expect(vertical_sky[static_cast<size_t>((9 * CHUNK_SIZE + 7) * CHUNK_SIZE +
                                          7)] == 0,
         "vertical seed is not equivalent to completed skylight");

  UChunk &chunk = *world.GetChunkManager().GetChunk({0, 0, 0});
  Expect(InstallComputedLight(chunk, result.chunks.front().light_packed,
                              result.include_skylight,
                              result.include_block_light),
         "computed field is installed");
  Expect(chunk.GetSkyLightLocal(shaded) == expected,
         "lateral sky survives apply");
  const auto revision = chunk.GetLightFieldRevision();
  Expect(!InstallComputedLight(chunk, result.chunks.front().light_packed, true,
                               true),
         "repeated result does not mutate the light field");
  Expect(chunk.GetLightFieldRevision() == revision,
         "repeated apply cannot stale meshes");
}

void TestSkylightReadsGeometryAbovePartialBand()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;

  // Target only chunk y=0, with an opaque ceiling two chunks above it.
  // The source light solver scans above each target slice, so this geometry
  // must be part of the immutable read context even though it is not relit.
  world.SetBlock({0, 0, 0}, 8);
  for (int x = 0; x < CHUNK_SIZE; ++x)
    for (int z = 0; z < CHUNK_SIZE; ++z)
      world.SetBlock({x, 32, z}, 8);

  RelightJobSpec spec;
  spec.block_positions.push_back({8, 0, 8});
  spec.min_world_y = 0;
  spec.max_world_y = CHUNK_SIZE - 1;
  spec.column_center_only = true;
  spec.include_block_light = false;
  auto snapshot = UChunkRelightSnapshot::Capture(world, spec);
  Expect(snapshot.GetBlock({8, 32, 8}) == 8,
         "upper opaque geometry is present in sky context");
  Expect(snapshot.GetCapturedSkyContextChunks() >= 1,
         "non-air sky context chunks are accounted");

  const auto result = snapshot.Compute(registry);
  Expect(result.chunks.size() == 1, "partial target computes only its chunk");
  const auto index = static_cast<size_t>(UChunk::LocalIndex({8, 15, 8}));
  Expect(UnpackSky(result.chunks.front().light_packed[index]) == 0,
         "opaque geometry above partial band blocks direct skylight");

  const ChunkInputStamp *roof_stamp = nullptr;
  for (const auto &stamp : result.read_set)
  {
    if (stamp.coord == glm::ivec3(0, 2, 0))
    {
      roof_stamp = &stamp;
      break;
    }
  }
  Expect(roof_stamp != nullptr && roof_stamp->readsContent &&
             !roof_stamp->readsLight,
         "sky context validates geometry without depending on upper light");
  Expect(roof_stamp->Matches(world.GetChunkManager().GetChunk({0, 2, 0})),
         "unchanged upper geometry validates");
  world.SetBlock({8, 32, 8}, BLOCK_AIR);
  Expect(!roof_stamp->Matches(world.GetChunkManager().GetChunk({0, 2, 0})),
         "upper geometry change invalidates the captured result");
}
} // namespace

int main()
{
  TestDomainsAndRevision();
  TestLateralSkylightSurvivesInstall();
  TestSkylightReadsGeometryAbovePartialBand();
  std::cout << "relight_result_install_test: OK\n";
}
