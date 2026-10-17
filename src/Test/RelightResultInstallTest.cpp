#include "World/Lighting/RelightResultInstall.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/GpuSkylightColumnSeed.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
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

void TestHorizontalLightBoundaryChangeMask()
{
  using namespace cutum;
  std::array<uint8_t, CHUNK_VOLUME> before{};
  std::array<uint8_t, CHUNK_VOLUME> computed{};
  const auto set = [&](glm::ivec3 local, int sky, int block)
  {
    computed[static_cast<size_t>(UChunk::LocalIndex(local))] =
        PackLight(sky, block);
  };

  set({0, 4, 7}, 0, 1);
  Expect(ChangedHorizontalLightBoundaryMask(before, computed, true, true) ==
             kHorizontalLightBoundaryXNegative,
         "x- blocklight changes identify only the x- face");

  computed.fill(0);
  set({8, 4, 7}, 0, 1);
  Expect(ChangedHorizontalLightBoundaryMask(before, computed, true, true) ==
             0,
         "interior-only changes do not create neighbor dependencies");

  computed.fill(0);
  set({6, 4, CHUNK_SIZE - 1}, 0, 1);
  Expect(ChangedHorizontalLightBoundaryMask(before, computed, true, false) ==
             0,
         "changes outside the installed light domains are ignored");
  set({6, 4, CHUNK_SIZE - 1}, 1, 1);
  Expect(ChangedHorizontalLightBoundaryMask(before, computed, true, false) ==
             kHorizontalLightBoundaryZPositive,
         "z+ skylight changes identify the z+ face");

  computed.fill(0);
  set({0, 4, CHUNK_SIZE - 1}, 1, 0);
  Expect(ChangedHorizontalLightBoundaryMask(before, computed, true, true) ==
             (kHorizontalLightBoundaryXNegative |
              kHorizontalLightBoundaryZPositive),
         "corner changes identify both horizontal neighbors");

  computed.fill(0);
  set({6, 0, 7}, 1, 0);
  Expect(ChangedHorizontalLightBoundaryMask(before, computed, true, true) ==
             0,
         "vertical-only boundary changes do not schedule horizontal work");
}

void TestHorizontalBoundaryDependencyWaveSettles()
{
  using namespace cutum;
  constexpr BlockId kStone = 8;
  constexpr BlockId kTorch = 50;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = BlockPhysicsProfile::Solid();
  BlockDefinition torch;
  torch.Name = "torch";
  torch.Physics = BlockPhysicsProfile::Solid();
  torch.Render.Style = BlockRenderStyle::Cross;
  torch.Lighting.Emission = 14;
  definitions->ReplaceAll({{kStone, stone}, {kTorch, torch}},
                          {{"stone", kStone}, {"torch", kTorch}});
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;
  world.SetBlock({CHUNK_SIZE, 1, 0}, kStone); // Ensure +x neighbor is resident.
  world.SetBlock({CHUNK_SIZE - 1, 1, 8}, kTorch);

  std::deque<glm::ivec2> pending{{0, 0}};
  int submitted_jobs = 0;
  constexpr int kMaxJobs = 8;
  const std::array<glm::ivec3, 4> offsets = {
      glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1)};
  constexpr std::array<uint8_t, 4> face_bits = {
      kHorizontalLightBoundaryXNegative, kHorizontalLightBoundaryXPositive,
      kHorizontalLightBoundaryZNegative, kHorizontalLightBoundaryZPositive};
  while (!pending.empty() && submitted_jobs < kMaxJobs)
  {
    const glm::ivec2 column = pending.front();
    pending.pop_front();
    RelightJobSpec spec;
    spec.block_positions =
        {glm::ivec3(column.x * CHUNK_SIZE, 0, column.y * CHUNK_SIZE)};
    spec.min_world_y = 0;
    spec.max_world_y = CHUNK_SIZE - 1;
    spec.include_skylight = false;
    spec.include_block_light = true;
    spec.frontier_iterations = 1;
    spec.column_center_only = true;
    spec.job_id = static_cast<uint64_t>(++submitted_jobs);
    const auto result = UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
    Expect(result.chunks.size() == 1,
           "a center-only dependency job installs just its target column");

    for (const RelightChunkLightData &chunk_data : result.chunks)
    {
      UChunk *chunk = world.GetChunkManager().GetChunk(chunk_data.coord);
      Expect(chunk != nullptr, "dependency target remains resident");
      const uint8_t face_mask = ChangedHorizontalLightBoundaryMask(
          chunk->GetLightData(), chunk_data.light_packed,
          result.include_skylight, result.include_block_light);
      if (!InstallComputedLight(*chunk, chunk_data.light_packed,
                                result.include_skylight,
                                result.include_block_light))
      {
        continue;
      }
      for (size_t face = 0; face < offsets.size(); ++face)
      {
        if ((face_mask & face_bits[face]) == 0)
        {
          continue;
        }
        const glm::ivec3 neighbor = chunk_data.coord + offsets[face];
        if (!world.GetChunkManager().HasChunk(neighbor))
        {
          continue;
        }
        const glm::ivec2 neighbor_column(neighbor.x, neighbor.z);
        if (std::find(pending.begin(), pending.end(), neighbor_column) ==
            pending.end())
        {
          pending.push_back(neighbor_column);
        }
      }
    }
  }

  Expect(pending.empty(), "horizontal boundary dependency wave reaches a fixed point");
  Expect(submitted_jobs == 3,
         "source, neighbor, and one return check settle without a repeated ring");
  const UChunk *neighbor = world.GetChunkManager().GetChunk({1, 0, 0});
  Expect(neighbor != nullptr &&
             neighbor->GetBlockLightLocal({0, 1, 8}) == 13,
         "neighbor receives the torch light across the shared face");
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

void TestHorizontalSkylightBoundarySeedsAirAndWater()
{
  using namespace cutum;
  constexpr BlockId kStone = 8;
  constexpr BlockId kWater = 9;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = BlockPhysicsProfile::Solid();
  BlockDefinition water;
  water.Name = "water";
  water.Physics = BlockPhysicsProfile::FromPreset("water");
  water.Render.Transparent = true;
  water.Render.Style = BlockRenderStyle::Fluid;
  definitions->ReplaceAll({{kStone, stone}, {kWater, water}},
                          {{"stone", kStone}, {"water", kWater}});
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;

  // A complete opaque ceiling suppresses direct skylight in the target
  // chunk. Only the pre-lit x- neighbor face can seed the two cells below it.
  for (int x = 0; x < CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < CHUNK_SIZE; ++z)
    {
      world.SetBlock({x, 9, z}, kStone);
    }
  }
  world.SetBlock({0, 8, 9}, kWater);
  world.SetBlock({-CHUNK_SIZE, 0, 0}, kStone); // Materialize x- neighbor.
  UChunk *neighbor = world.GetChunkManager().GetChunk({-1, 0, 0});
  Expect(neighbor != nullptr, "neighbor chunk exists");
  neighbor->SetLightLocal({CHUNK_SIZE - 1, 8, 8}, kMaxLightLevel, 0);
  neighbor->SetLightLocal({CHUNK_SIZE - 1, 8, 9}, kMaxLightLevel, 0);

  RelightJobSpec spec;
  spec.block_positions = {{0, 8, 8}};
  spec.min_world_y = 0;
  spec.max_world_y = CHUNK_SIZE - 1;
  spec.column_center_only = true;
  spec.include_block_light = false;
  spec.frontier_iterations = 0;
  auto snapshot = UChunkRelightSnapshot::Capture(world, spec);
  const auto result = snapshot.Compute(registry);
  Expect(result.chunks.size() == 1,
         "center-only boundary seed writes one target chunk");

  const auto &light = result.chunks.front().light_packed;
  const auto sky_at = [&light](glm::ivec3 local) {
    return UnpackSky(light[static_cast<size_t>(UChunk::LocalIndex(local))]);
  };
  Expect(sky_at({0, 8, 8}) == kMaxLightLevel - 1,
         "neighbor skylight attenuates once into an air boundary cell");
  Expect(sky_at({0, 8, 9}) == kMaxLightLevel - 2,
         "neighbor skylight uses liquid attenuation at a water boundary cell");
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
  const ChunkInputStamp *absent_context_stamp = nullptr;
  for (const auto &stamp : result.read_set)
  {
    if (stamp.coord == glm::ivec3(0, 2, 0))
    {
      roof_stamp = &stamp;
    }
    if (stamp.coord == glm::ivec3(0, 3, 0))
      absent_context_stamp = &stamp;
  }
  Expect(roof_stamp != nullptr && roof_stamp->readsContent &&
             !roof_stamp->readsLight,
         "sky context validates geometry without depending on upper light");
  Expect(roof_stamp->Matches(world.GetChunkManager().GetChunk({0, 2, 0})),
         "unchanged upper geometry validates");
  world.SetBlock({8, 32, 8}, BLOCK_AIR);
  Expect(!roof_stamp->Matches(world.GetChunkManager().GetChunk({0, 2, 0})),
         "upper geometry change invalidates the captured result");
  Expect(absent_context_stamp != nullptr &&
             absent_context_stamp->Matches(nullptr),
         "absent upper context validates while still absent");
  world.SetBlock({8, 48, 8}, 8);
  Expect(!absent_context_stamp->Matches(
             world.GetChunkManager().GetChunk({0, 3, 0})),
         "newly streamed upper geometry invalidates the captured result");
}

void TestSkylightContextDoesNotExpandThreeByThreeWriteSet()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;

  for (int cx = -1; cx <= 1; ++cx)
  {
    for (int cz = -1; cz <= 1; ++cz)
    {
      world.SetBlock({cx * CHUNK_SIZE, 0, cz * CHUNK_SIZE}, 8);
      world.SetBlock({cx * CHUNK_SIZE, 32, cz * CHUNK_SIZE}, 8);
    }
  }

  RelightJobSpec spec;
  spec.block_positions.push_back({8, 0, 8});
  spec.min_world_y = 0;
  spec.max_world_y = CHUNK_SIZE - 1;
  spec.column_center_only = false;
  spec.include_block_light = false;
  spec.frontier_iterations = 0;
  auto snapshot = UChunkRelightSnapshot::Capture(world, spec);
  Expect(snapshot.GetCapturedFullChunks() == 9,
         "3x3 target band captures all nine writable chunks");
  Expect(snapshot.GetCapturedSkyContextChunks() == 9,
         "3x3 target captures resident upper context per column");
  Expect(snapshot.GetBlock({CHUNK_SIZE, 32, CHUNK_SIZE}) == 8,
         "3x3 upper context geometry is readable");
  Expect(!snapshot.HasChunk({1, 2, 1}),
         "3x3 upper context does not become a writable target");

  const auto result = snapshot.Compute(registry);
  Expect(result.chunks.size() == 9,
         "3x3 compute returns target chunks only");
  const auto context_stamp = std::find_if(
      result.read_set.begin(), result.read_set.end(),
      [](const ChunkInputStamp &stamp) {
        return stamp.coord == glm::ivec3(1, 2, 1);
      });
  Expect(context_stamp != result.read_set.end() &&
             context_stamp->readsContent && !context_stamp->readsLight,
         "3x3 context geometry is validated independently of its light");
}
} // namespace

int main()
{
  TestDomainsAndRevision();
  TestHorizontalLightBoundaryChangeMask();
  TestHorizontalBoundaryDependencyWaveSettles();
  TestLateralSkylightSurvivesInstall();
  TestHorizontalSkylightBoundarySeedsAirAndWater();
  TestSkylightReadsGeometryAbovePartialBand();
  TestSkylightContextDoesNotExpandThreeByThreeWriteSet();
  std::cout << "relight_result_install_test: OK\n";
}
