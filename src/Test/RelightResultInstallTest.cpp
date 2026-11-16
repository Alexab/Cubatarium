#include "World/Lighting/RelightResultInstall.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/GpuSkylightColumnSeed.h"
#include "World/Lighting/RelightInputValidation.h"
#include "World/Streaming/MeshLitGate.h"

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

void TestLightChannelIdentityAndBufferedWrites()
{
  using namespace cutum;
  UChunk chunk({-1, -3, 2});
  chunk.SetLightLocal({2, 3, 4}, 0, 7);
  Expect(chunk.GetSkyLightRevision() == 0 && chunk.GetBlockLightRevision() == 1,
         "block-only write preserves sky input identity");
  chunk.SetLightLocal({2, 3, 4}, 9, 7);
  Expect(chunk.GetSkyLightRevision() == 1 && chunk.GetBlockLightRevision() == 1,
         "sky-only write preserves block-light identity");
  chunk.ClearLightLocal({2, 3, 4});
  Expect(chunk.GetSkyLightRevision() == 2 && chunk.GetBlockLightRevision() == 2,
         "clear invalidates both changed channels");
  auto data = chunk.GetLightData();
  data[UChunk::LocalIndex({2, 3, 4})] = PackLight(0, 11);
  Expect(chunk.ReplaceLightData(data), "atomic block replacement changes data");
  Expect(chunk.GetSkyLightRevision() == 2 && chunk.GetBlockLightRevision() == 3,
         "atomic replacement tracks only the changed channel");
  const auto revision = chunk.GetLightFieldRevision();
  Expect(!chunk.ReplaceLightData(data) &&
             chunk.GetLightFieldRevision() == revision,
         "same field does not invalidate any published identity");
  Expect(
      MeshCandidateMatchesSettledDemand(true, true, true, 5, 5, 5, 0, 0, 0, 0),
      "explicitly settled zero field can support a current dark candidate");
  Expect(!MeshCandidateMatchesSettledDemand(true, true, false, 5, 5, 5, 0, 0, 0,
                                            0),
         "zero field equality without a computation proof remains unready");
  chunk.SetBlockLocal({2, 3, 4}, 8);
  chunk.SetFluidLocal({2, 3, 4}, FluidCellState::Flowing(3));
  Expect(chunk.GetSkyLightRevision() == 2 && chunk.GetBlockLightRevision() == 3,
         "geometry and fluid have their own content identity");
  const auto incarnation = chunk.GetIncarnation();
  chunk.ResetForReuse(chunk.GetCoord());
  Expect(chunk.GetIncarnation() != incarnation &&
             chunk.GetSkyLightRevision() == 0 &&
             chunk.GetBlockLightRevision() == 0,
         "recycled channel revisions are scoped by a new incarnation");

  UBlockWorld world;
  const glm::ivec3 coord(-1, -3, 2);
  const glm::ivec3 cell = coord * CHUNK_SIZE + glm::ivec3(2, 3, 4);
  UChunkBuffer sparse;
  sparse.SetLightPacked(cell, PackLight(3, 5));
  Expect(!sparse.IsEmpty(), "a light-only buffer contains executable work");
  sparse.ApplyToChunk(world, coord);
  const UChunk *loaded = world.GetChunkManager().GetChunk(coord);
  Expect(loaded && loaded->GetLightPackedLocal({2, 3, 4}) == PackLight(3, 5),
         "sparse buffered light reaches its signed section");
  const auto buffered_revision = loaded->GetLightFieldRevision();
  sparse.ApplyToChunk(world, coord);
  Expect(loaded->GetLightFieldRevision() == buffered_revision,
         "repeated buffered apply retains light identity");
  sparse.SetLightPacked(cell, 0);
  sparse.ApplyToChunk(world, coord);
  Expect(loaded->GetLightPackedLocal({2, 3, 4}) == 0,
         "explicit zero light is a removal, not missing buffered work");
  UChunkBuffer dense;
  dense.SetChunkLightData(coord, loaded->GetLightData());
  const auto zero_revision = loaded->GetLightFieldRevision();
  dense.ApplyToChunk(world, coord);
  dense.ApplyTo(world);
  Expect(loaded->GetLightFieldRevision() == zero_revision,
         "unchanged dense IO applies do not invalidate light identity");
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
  Expect(ChangedLightBoundaryMask(before, computed, true, true) ==
             kVerticalLightBoundaryYNegative,
         "bottom skylight boundary carries a vertical dependency");
  Expect(ChangedLightBoundaryMask(computed, before, true, true) ==
             kVerticalLightBoundaryYNegative,
         "light removal carries the same vertical dependency as addition");
  Expect(ChangedLightBoundaryMask(before, computed, false, true) == 0,
         "vertical mask ignores a light domain that is not installed");
  computed.fill(0);
  set({6, CHUNK_SIZE - 1, 7}, 0, 3);
  Expect(ChangedLightBoundaryMask(before, computed, false, true) ==
             kVerticalLightBoundaryYPositive,
         "top blocklight boundary has the correct positive Y direction");
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
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
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
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });

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
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });

  // Target only chunk y=0, with an opaque ceiling two chunks above it.
  // The source light solver scans above each target slice, so this geometry
  // must be part of the immutable read context even though it is not relit.
  world.SetBlock({0, 0, 0}, 8);
  for (int x = -CHUNK_SIZE; x < 2 * CHUNK_SIZE; ++x)
    for (int z = -CHUNK_SIZE; z < 2 * CHUNK_SIZE; ++z)
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
  Expect(result.chunks.size() == 9, "3x3 compute returns target chunks only");
  const auto context_stamp =
      std::find_if(result.read_set.begin(), result.read_set.end(),
                   [](const ChunkInputStamp &stamp)
                   { return stamp.coord == glm::ivec3(1, 2, 1); });
  Expect(context_stamp != result.read_set.end() &&
             context_stamp->readsContent && !context_stamp->readsLight,
         "3x3 context geometry is validated independently of its light");
}
void TestVerticalBoundaryDependencyAdditionAndRemoval()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = BlockPhysicsProfile::Solid();
  BlockDefinition torch;
  torch.Name = "torch";
  torch.Physics = BlockPhysicsProfile::Solid();
  torch.Render.Style = BlockRenderStyle::Cross;
  torch.Lighting.Emission = 14;
  definitions->ReplaceAll({{8, stone}, {50, torch}},
                          {{"stone", 8}, {"torch", 50}});
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;
  world.SetBlock({0, -48, 0}, 8);
  world.SetBlock({0, -32, 0}, 8);
  const glm::ivec3 source(8, -33, 8);
  world.SetBlock(source, 50);
  const std::array<glm::ivec3, 6> offsets = {
      glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),  glm::ivec3(0, 0, -1),
      glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0), glm::ivec3(0, 1, 0)};
  const auto settle = [&]()
  {
    std::deque<glm::ivec3> pending{{0, -3, 0}};
    int jobs = 0;
    while (!pending.empty() && jobs++ < 128)
    {
      const glm::ivec3 coord = pending.front();
      pending.pop_front();
      RelightJobSpec spec;
      spec.block_positions = {coord * CHUNK_SIZE};
      spec.min_world_y = coord.y * CHUNK_SIZE;
      spec.max_world_y = spec.min_world_y + CHUNK_SIZE - 1;
      spec.column_center_only = true;
      spec.include_skylight = false;
      const auto result =
          UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
      for (const auto &computed : result.chunks)
      {
        auto *chunk = world.GetChunkManager().GetChunk(computed.coord);
        const uint8_t mask = ChangedLightBoundaryMask(
            chunk->GetLightData(), computed.light_packed, false, true);
        if (!InstallComputedLight(*chunk, computed.light_packed, false, true))
          continue;
        for (size_t face = 0; face < offsets.size(); ++face)
        {
          const glm::ivec3 neighbor = computed.coord + offsets[face];
          if ((mask & (1u << face)) &&
              world.GetChunkManager().HasChunk(neighbor) &&
              std::find(pending.begin(), pending.end(), neighbor) ==
                  pending.end())
            pending.push_back(neighbor);
        }
      }
    }
    Expect(pending.empty(), "vertical dependency wave reaches a fixed point");
  };
  settle();
  const UChunk *upper = world.GetChunkManager().GetChunk({0, -2, 0});
  Expect(
      upper->GetBlockLightLocal({8, 0, 8}) == 13,
      "partial negative-height relight crosses the vertical section boundary");
  world.SetBlock(source, BLOCK_AIR);
  settle();
  for (const glm::ivec3 coord : {glm::ivec3(0, -3, 0), glm::ivec3(0, -2, 0)})
    for (const uint8_t packed :
         world.GetChunkManager().GetChunk(coord)->GetLightData())
      Expect(UnpackBlock(packed) == 0,
             "removing the source clears light from both vertical neighbors");
}

void TestRelightAbsenceEvidenceChangesWithoutResidency()
{
  using namespace cutum;
  UBlockWorld world;
  world.SetBlock({8, -40, 8}, 8);
  const glm::ivec3 absent(0, -2, 0);
  bool known_sky = false;
  world.SetKnownOpenSkyAbsentQuery([&](glm::ivec3 coord)
                                   { return known_sky && coord == absent; });
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  UBlockRegistry registry(nullptr, definitions);
  RelightJobSpec spec;
  spec.block_positions = {{8, -40, 8}};
  spec.min_world_y = -48;
  spec.max_world_y = -33;
  spec.column_center_only = true;
  auto result = UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
  size_t witnessed = 0;
  for (const auto &stamp : result.read_set)
    if (stamp.coord == absent)
    {
      ++witnessed;
      Expect(stamp.missingEvidence == MissingChunkEvidence::Unknown,
             "relight distinguishes unknown air from completed sky");
      Expect(world.InputStampStillMatches(stamp),
             "unchanged unknown dependency matches its capture identity");
    }
  Expect(witnessed > 0, "partial-band relight captures absent upper context");
  known_sky = true;
  Expect(!world.GetChunkManager().GetChunk(absent),
         "proof transition must not rely on a chunk load");
  for (const auto &stamp : result.read_set)
    if (stamp.coord == absent)
      Expect(!world.InputStampStillMatches(stamp),
             "completion evidence invalidates an unknown-input relight");
  result = UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
  known_sky = false;
  for (const auto &stamp : result.read_set)
    if (stamp.coord == absent)
      Expect(!world.InputStampStillMatches(stamp),
             "revoking sky proof invalidates a completed-input relight");
}

void TestRelightInputContextAndSeparateChannels()
{
  using namespace cutum;
  UBlockWorld world;
  world.SetBlock({8, -40, 8}, 8);
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  RelightJobSpec spec;
  spec.block_positions = {{8, -40, 8}};
  spec.min_world_y = -48;
  spec.max_world_y = -33;
  spec.column_center_only = true;
  spec.include_skylight = true;
  spec.include_block_light = false;
  auto result = UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
  // Exercise the validator's explicit light-reading contract independently of
  // the geometry-only backend, which no longer reads neighboring light bytes.
  result.read_set.push_back(world.CaptureInputStamp({0, -3, 0}));
  const auto validate = [&](bool known = false)
  {
    return ValidateRelightInputs(
        result, world, registry.GetDefinitionsCatalogSnapshot(), known);
  };
  Expect(validate() == RelightInputValidation::Current,
         "captured world and selected light domain match");
  Expect(validate(true) == RelightInputValidation::UnknownInput,
         "transient unknown inputs cannot certify a completed calculation");
  auto *chunk = world.GetChunkManager().GetChunk({0, -3, 0});
  chunk->SetLightLocal({2, 3, 4}, 0, 7);
  Expect(validate() == RelightInputValidation::Current,
         "block-only mutation does not reject a sky-only computation");
  chunk->SetLightLocal({2, 3, 4}, 9, 7);
  Expect(validate() == RelightInputValidation::ReadSet,
         "sky mutation rejects a sky computation");
  spec.include_skylight = false;
  spec.include_block_light = true;
  result = UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
  result.read_set.push_back(world.CaptureInputStamp({0, -3, 0}));
  chunk->SetLightLocal({2, 3, 4}, 10, 7);
  Expect(validate() == RelightInputValidation::Current,
         "sky-only mutation does not reject a block-only computation");
  chunk->SetLightLocal({2, 3, 4}, 10, 8);
  Expect(validate() == RelightInputValidation::ReadSet,
         "block mutation rejects a block computation");
  result = UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
  const auto profile = world.GetVerticalProfile();
  auto extended = profile;
  extended.MaxY += CHUNK_SIZE;
  world.SetVerticalProfile(extended);
  Expect(
      validate() == RelightInputValidation::Profile,
      "changed ceiling rejects a result even with identical captured chunks");
  world.SetVerticalProfile(profile);
  world.SetLightingConfigurationIdentity(1);
  Expect(validate() == RelightInputValidation::Configuration,
         "changed lighting mode rejects an obsolete algorithm result");
  world.SetLightingConfigurationIdentity(0);
  auto other = std::make_shared<UBlockDefinitionStorage>();
  Expect(ValidateRelightInputs(result, world, other->GetCatalogSnapshot()) ==
             RelightInputValidation::Catalog,
         "another material catalog cannot inherit the calculation");
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  result = UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
  Expect(validate(true) == RelightInputValidation::Current,
         "known resident and proven-air inputs can support completion");
  result.input_context_captured = false;
  Expect(validate() == RelightInputValidation::MissingContext,
         "missing provenance cannot inherit the current configuration");
}

void TestSectionLightUsesKnownEmitterGeometry()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition torch;
  torch.Name = "torch";
  torch.Physics = BlockPhysicsProfile::Solid();
  torch.Render.Style = BlockRenderStyle::Cross;
  torch.Lighting.Emission = 14;
  definitions->ReplaceAll({{50, torch}}, {{"torch", 50}});
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  world.GetChunkManager().EnsureChunk({0, -3, 0});
  world.SetBlock({16, -40, 8}, 50);
  auto *source = world.GetChunkManager().GetChunk({1, -3, 0});
  for (int neighbor_field : {0, 15})
  {
    source->SetLightLocal({0, 8, 8}, 0, neighbor_field);
    for (bool center_only : {false, true})
    {
      RelightJobSpec spec;
      spec.block_positions = {{8, -40, 8}};
      spec.min_world_y = -48;
      spec.max_world_y = -33;
      spec.column_center_only = center_only;
      spec.include_skylight = false;
      const auto result =
          UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
      const auto target = std::find_if(
          result.chunks.begin(), result.chunks.end(),
          [](const auto &c) { return c.coord == glm::ivec3(0, -3, 0); });
      Expect(target != result.chunks.end(), "section target is computed");
      Expect(UnpackBlock(
                 target->light_packed[UChunk::LocalIndex({15, 8, 8})]) == 13,
             "known neighbor emitter lights section independently of neighbor "
             "light readiness");
      Expect(source->GetBlockLightLocal({0, 8, 8}) == neighbor_field,
             "read-only halo computation does not mutate source world light");
    }
  }
}

// Independent finite-volume reference: Jacobi relaxation over a dense voxel
// array, without production chunk traversal, input stamps, or bucket queues.
void TestGeometryHaloAgainstDenseReference()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone, torch;
  stone.Physics = BlockPhysicsProfile::Solid();
  torch.Physics = BlockPhysicsProfile::Solid();
  torch.Render.Style = BlockRenderStyle::Cross;
  torch.Lighting.Emission = 14;
  definitions->ReplaceAll({{8, stone}, {50, torch}},
                          {{"stone", 8}, {"torch", 50}});
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  const glm::ivec3 origin(-32, -48, -16);
  world.GetChunkManager().EnsureChunk({-1, -2, 0});
  constexpr int width = 48;
  const auto index = [=](int x, int y, int z)
  { return x + width * (y + width * z); };
  std::vector<BlockId> cells(width * width * width, BLOCK_AIR);
  uint32_t random = 0x23a94u;
  for (int z = 1; z < width - 1; ++z)
    for (int y = 1; y < width - 1; ++y)
      for (int x = 1; x < width - 1; ++x)
      {
        random = random * 1664525u + 1013904223u;
        if ((random >> 24) < 28)
        {
          cells[index(x, y, z)] = 8;
          world.SetBlock(origin + glm::ivec3(x, y, z), 8);
        }
      }
  const std::array<glm::ivec3, 6> sources = {{{15, 23, 24},
                                              {32, 24, 23},
                                              {24, 15, 23},
                                              {23, 32, 24},
                                              {23, 24, 15},
                                              {24, 23, 32}}};
  RelightJobSpec spec;
  spec.block_positions = {origin + glm::ivec3(24)};
  spec.min_world_y = -32;
  spec.max_world_y = -17;
  spec.column_center_only = true;
  spec.include_skylight = false;
  for (int scenario = 0; scenario < 3; ++scenario)
  {
    for (size_t n = 0; n < sources.size(); ++n)
    {
      const auto pos = sources[n];
      const BlockId id = scenario == 1 && n % 2 == 0 ? BLOCK_AIR : 50;
      cells[index(pos.x, pos.y, pos.z)] = id;
      world.SetBlock(origin + pos, id);
    }
    if (scenario == 2)
    {
      torch.Lighting.Emission = 6;
      definitions->ReplaceAll({{8, stone}, {50, torch}},
                              {{"stone", 8}, {"torch", 50}});
    }
    const int emission = scenario == 2 ? 6 : 14;
    std::vector<uint8_t> reference(cells.size(), 0), next;
    for (size_t i = 0; i < cells.size(); ++i)
      if (cells[i] == 50)
        reference[i] = static_cast<uint8_t>(emission);
    for (int iteration = 0; iteration < 15; ++iteration)
    {
      next = reference;
      for (int z = 1; z < width - 1; ++z)
        for (int y = 1; y < width - 1; ++y)
          for (int x = 1; x < width - 1; ++x)
          {
            const int i = index(x, y, z);
            if (cells[i] == 8)
              continue;
            const int adjacent = std::max(
                {reference[i - 1], reference[i + 1], reference[i - width],
                 reference[i + width], reference[i - width * width],
                 reference[i + width * width]});
            next[i] =
                static_cast<uint8_t>(std::max<int>(reference[i], adjacent - 1));
          }
      reference.swap(next);
    }
    auto snapshot = UChunkRelightSnapshot::Capture(world, spec);
    auto result = snapshot.Compute(registry);
    Expect(result.inputs_complete && result.chunks.size() == 1,
           "complete geometry halo computes exactly one requested section");
    for (const auto &stamp : result.read_set)
      Expect(!stamp.readsLight,
             "geometry solver has no neighboring field dependency");
    for (int z = 0; z < 16; ++z)
      for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
          Expect(UnpackBlock(result.chunks[0].light_packed[UChunk::LocalIndex(
                     {x, y, z})]) == reference[index(x + 16, y + 16, z + 16)],
                 "all 4096 target cells match independent dense reference "
                 "after source changes");
    const auto repeated = snapshot.Compute(registry);
    Expect(repeated.chunks[0].light_packed == result.chunks[0].light_packed,
           "reusing immutable snapshot recomputes the same field");
  }
}

void TestUnknownGeometryAndPinnedCatalog()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition torch;
  torch.Physics = BlockPhysicsProfile::Solid();
  torch.Render.Style = BlockRenderStyle::Cross;
  torch.Lighting.Emission = 14;
  definitions->ReplaceAll({{50, torch}}, {{"torch", 50}});
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;
  world.GetChunkManager().EnsureChunk({0, -3, 0});
  RelightJobSpec spec;
  spec.block_positions = {{8, -40, 8}};
  spec.min_world_y = -48;
  spec.max_world_y = -33;
  spec.column_center_only = true;
  auto unknown = UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
  Expect(!unknown.inputs_complete && unknown.unknown_input_chunks > 0,
         "unknown upper geometry cannot certify lighting");
  Expect(UnpackSky(
             unknown.chunks[0].light_packed[UChunk::LocalIndex({8, 8, 8})]) ==
             0,
         "unknown upper geometry is never interpreted as clear sky");
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  auto clear = UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
  Expect(clear.inputs_complete &&
             UnpackSky(
                 clear.chunks[0].light_packed[UChunk::LocalIndex({8, 8, 8})]) ==
                 15,
         "proven empty upper geometry admits direct sky");
  world.SetBlock({16, -40, 8}, 50);
  spec.include_skylight = false;
  auto captured = UChunkRelightSnapshot::Capture(world, spec);
  captured.SetInputCatalog(registry.GetDefinitionsCatalogSnapshot());
  torch.Lighting.Emission = 6;
  definitions->ReplaceAll({{50, torch}}, {{"torch", 50}});
  const auto old = captured.Compute(registry);
  Expect(UnpackBlock(
             old.chunks[0].light_packed[UChunk::LocalIndex({15, 8, 8})]) == 13,
         "captured immutable catalog survives a concurrent material reload");
  Expect(ValidateRelightInputs(old, world,
                               registry.GetDefinitionsCatalogSnapshot()) ==
             RelightInputValidation::Catalog,
         "old catalog result is rejected for installation");
  const auto fresh =
      UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
  Expect(UnpackBlock(
             fresh.chunks[0].light_packed[UChunk::LocalIndex({15, 8, 8})]) == 5,
         "new computation uses changed material emission");
}

} // namespace

int main(int argc, char **argv)
{
  if (argc == 2 && std::string(argv[1]) == "--geometry-halo")
  {
    TestSectionLightUsesKnownEmitterGeometry();
    TestGeometryHaloAgainstDenseReference();
    TestUnknownGeometryAndPinnedCatalog();
    std::cout << "section_light_geometry_test: OK\n";
    return 0;
  }
  TestDomainsAndRevision();
  TestLightChannelIdentityAndBufferedWrites();
  TestHorizontalLightBoundaryChangeMask();
  TestHorizontalBoundaryDependencyWaveSettles();
  TestLateralSkylightSurvivesInstall();
  TestHorizontalSkylightBoundarySeedsAirAndWater();
  TestSkylightReadsGeometryAbovePartialBand();
  TestSkylightContextDoesNotExpandThreeByThreeWriteSet();
  TestVerticalBoundaryDependencyAdditionAndRemoval();
  TestRelightAbsenceEvidenceChangesWithoutResidency();
  TestRelightInputContextAndSeparateChannels();
  TestSectionLightUsesKnownEmitterGeometry();
  TestGeometryHaloAgainstDenseReference();
  TestUnknownGeometryAndPinnedCatalog();
  std::cout << "relight_result_install_test: OK\n";
}
