#include "World/Lighting/RelightResultInstall.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/GpuSkylightColumnSeed.h"
#include "World/Lighting/LightCalculationSettlement.h"
#include "World/Lighting/PendingLightCoverage.h"
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
  Expect(
      MeshCandidateMatchesSettledDemand(true, true, true, 2, 2, 0, 2, 2, 2, 0),
      "native dark replacement: fresh source 2 satisfies a zero legacy demand "
      "floor");
  Expect(
      MeshCandidateMatchesSettledDemand(true, true, true, 5, 5, 3, 4, 4, 4, 2),
      "coalesced older demand is satisfied by the exact current proved "
      "candidate");
  Expect(
      !MeshCandidateMatchesSettledDemand(true, true, true, 5, 5, 6, 4, 4, 4, 2),
      "a newer geometry request remains unsatisfied");
  Expect(
      !MeshCandidateMatchesSettledDemand(true, true, true, 5, 5, 3, 4, 4, 4, 5),
      "a newer light request remains unsatisfied");
  Expect(!MeshCandidateMatchesSettledDemand(true, true, true, 4, 5, 3, 4, 4, 4,
                                            2) &&
             !MeshCandidateMatchesSettledDemand(true, true, true, 5, 5, 3, 3, 4,
                                                4, 2) &&
             !MeshCandidateMatchesSettledDemand(true, false, true, 5, 5, 3, 4,
                                                4, 4, 2),
         "older demand floors never authorize stale geometry, light or "
         "incarnation");

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
  torch.Lighting.Emission = 15;
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
  const std::array<glm::ivec3, 8> sources = {{{15, 23, 24},
                                              {32, 24, 23},
                                              {24, 15, 23},
                                              {23, 32, 24},
                                              {23, 24, 15},
                                              {24, 23, 32},
                                              {0, 24, 24},
                                              {47, 24, 24}}};
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
    const int emission = scenario == 2 ? 6 : 15;
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
    Expect(static_cast<bool>(result.chunks[0].padded_light),
           "geometry calculation retains the complete mesh sampling volume");
    for (int z = -2; z < 18; ++z)
      for (int y = -2; y < 18; ++y)
        for (int x = -2; x < 18; ++x)
          Expect(UnpackBlock(result.chunks[0]
                                 .padded_light
                                 ->cells[SectionLightVolume::Index(x, y, z)]) ==
                     reference[index(x + 16, y + 16, z + 16)],
                 "all 8000 padded cells match independent Jacobi reference "
                 "after source removal and material emission changes");
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

void TestClippedProfileDoesNotSeedOutOfWorldSource()
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
  world.SetVerticalProfile(WorldVerticalProfile(-63, 318));
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  // A loaded legacy slice can contain cells outside a clipped runtime profile.
  world.GetChunkManager().EnsureChunk({0, -4, 0});
  world.GetChunkManager().GetChunk({0, -4, 0})->SetBlockLocal({8, 0, 8}, 50);
  RelightJobSpec spec;
  spec.block_positions = {{8, -63, 8}};
  spec.min_world_y = -63;
  spec.max_world_y = -49;
  spec.column_center_only = true;
  spec.include_skylight = false;
  const auto result =
      UChunkRelightSnapshot::Capture(world, spec).Compute(registry);
  Expect(result.inputs_complete && result.chunks.size() == 1,
         "clipped profile retains known section inputs");
  Expect(UnpackBlock(
             result.chunks[0].light_packed[UChunk::LocalIndex({8, 1, 8})]) == 0,
         "source below runtime profile cannot emit through its clipped lower "
         "face");
}

void TestAbsentFieldInputStampIdentity()
{
  using namespace cutum;
  UBlockWorld world;
  bool known_air = true;
  world.SetKnownOpenSkyAbsentQuery([&](glm::ivec3) { return known_air; });
  const glm::ivec3 coord(2, -3, 1);
  auto &manager = world.GetChunkManager();
  const auto geometry_clock = manager.GetGeometryInputRevision();
  const auto geometry = world.CaptureInputStamp(coord, false, true);
  const auto missing = world.CaptureInputStamp(coord, true, true);
  auto field = manager.EnsureSectionLightField(coord);
  Expect(!world.InputStampStillMatches(missing) &&
             !world.InputStampLightDomainsStillMatch(missing, true, true) &&
             world.InputStampStillMatches(geometry),
         "absent field allocation must invalidate a missing light read, not "
         "geometry");
  const auto zero = world.CaptureInputStamp(coord, true, true);
  Expect(zero.lightIncarnation == field->GetIncarnation() && zero.light == 0 &&
             world.InputStampStillMatches(zero),
         "allocated zero-revision field has an explicit valid identity");
  SectionLightVolume volume;
  volume.cells.fill(0x21);
  manager.ReplaceComputedSectionLightVolume(coord, volume, true, true);
  Expect(!world.InputStampStillMatches(zero),
         "nonzero AIR field installation invalidates the zero light read");
  auto stamp = world.CaptureInputStamp(coord, true, true);
  volume.cells.fill(0x22);
  manager.ReplaceComputedSectionLightVolume(coord, volume, true, false);
  Expect(!world.InputStampStillMatches(stamp) &&
             world.InputStampLightDomainsStillMatch(stamp, false, true) &&
             !world.InputStampLightDomainsStillMatch(stamp, true, false),
         "AIR sky mutation invalidates only the selected sky domain");
  stamp = world.CaptureInputStamp(coord, true, true);
  volume.cells.fill(0x32);
  manager.ReplaceComputedSectionLightVolume(coord, volume, false, true);
  Expect(
      world.InputStampLightDomainsStillMatch(stamp, true, false) &&
          !world.InputStampLightDomainsStillMatch(stamp, false, true) &&
          world.InputStampStillMatches(geometry) &&
          manager.GetGeometryInputRevision() == geometry_clock &&
          manager.GetResidentChunkCount() == 0,
      "AIR block mutation invalidates block reads without creating geometry");
  manager.ReleaseAbsentSectionLightField(coord);
  manager.ReplaceComputedSectionLightVolume(coord, volume, true, true);
  stamp = world.CaptureInputStamp(coord, true, true);
  manager.ReleaseAbsentSectionLightField(coord);
  manager.ReplaceComputedSectionLightVolume(coord, volume, true, true);
  const auto replacement = world.CaptureInputStamp(coord, true, true);
  Expect(stamp.light == replacement.light &&
             stamp.skyLight == replacement.skyLight &&
             stamp.blockLight == replacement.blockLight &&
             stamp.lightIncarnation != replacement.lightIncarnation &&
             !world.InputStampStillMatches(stamp) &&
             !world.InputStampLightDomainsStillMatch(stamp, true, false) &&
             !world.InputStampLightDomainsStillMatch(stamp, false, true),
         "same-byte same-revision AIR reallocation invalidates old resource "
         "reads");
  known_air = false;
  Expect(!world.InputStampStillMatches(replacement) &&
             !world.InputStampLightDomainsStillMatch(replacement, true, true),
         "a field identity cannot substitute for current known-AIR geometry "
         "evidence");
  known_air = true;
  manager.EnsureChunk(coord);
  Expect(!world.InputStampStillMatches(replacement) &&
             manager.GetSectionLightField(coord)->GetIncarnation() ==
                 replacement.lightIncarnation,
         "geometry admission preserves the field and invalidates the old "
         "absence read");
}

void TestKnownAirLightResourceCalculation()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition emitter;
  emitter.Physics = BlockPhysicsProfile::Solid();
  emitter.Lighting.Emission = 15;
  definitions->ReplaceAll({{50, emitter}}, {{"emitter", 50}});
  UBlockRegistry registry(nullptr, definitions);
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  world.SetBlock({15, -40, 8}, 50);
  const glm::ivec3 target(1, -3, 0);
  auto &manager = world.GetChunkManager();
  const auto resident = manager.GetResidentChunkCount();
  const auto geometry_clock = manager.GetGeometryInputRevision();
  RelightJobSpec spec;
  spec.min_world_y = -64;
  spec.max_world_y = 319;
  spec.include_known_air_outputs = true;
  auto solve = [&]()
  {
    return UChunkRelightSnapshot::CaptureSections(world, spec, {target})
        .Compute(registry);
  };
  auto result = solve();
  Expect(
      result.inputs_complete && result.chunks.size() == 1 &&
          UnpackBlock(
              result.chunks[0].light_packed[UChunk::LocalIndex({0, 8, 8})]) ==
              14,
      "emission crosses into an entirely absent known-AIR section at negative "
      "Y");
  Expect(InstallComputedLight(world, result.chunks[0], true, true),
         "world installer accepts a validated padded known-AIR output");
  Expect(world.RecordLightCalculation(result, catalog) == 1 &&
             world.HasCompleteLightSamplingCalculation(target, catalog) &&
             world.GetCurrentLightSamplingVolume(target) &&
             !manager.HasChunk(target) &&
             manager.GetResidentChunkCount() == resident &&
             manager.GetGeometryInputRevision() == geometry_clock,
         "known-AIR field and physical receipt do not fabricate geometry or "
         "mutate geometry clock");
  const auto sample = world.SampleSceneLight({16, -40, 8});
  Expect(sample.state == SceneLightState::Computed && sample.IsReady() &&
             UnpackBlock(*sample.packed) == 14 && sample.field_incarnation != 0,
         "point sampler reads certified nonzero AIR field without geometry");
  const auto field = manager.GetSectionLightField(target);
  Expect(sample.field_incarnation == field->GetIncarnation() &&
             sample.field_revision == field->GetRevision(),
         "point sample identifies exact canonical resource and revision");
  const auto render = world.CaptureRenderLightSnapshot(target);
  Expect(render &&
             render->Volume() == world.GetCurrentLightSamplingVolume(target) &&
             render->FieldIncarnation() == field->GetIncarnation() &&
             render->FieldRevision() == field->GetRevision() &&
             render->MatchesGeometry(render->GeometryInputs()),
         "render snapshot retains exact certified padded AIR field and "
         "provenance");
  const auto geometry = render->GeometryInputs();
  for (size_t n = 0; n < geometry.stamps.size(); ++n)
  {
    auto changed = geometry;
    ++changed.stamps[n].content;
    Expect(!render->MatchesGeometry(changed),
           "every geometry content input participates in light compatibility");
    changed = geometry;
    ++changed.stamps[n].incarnation;
    Expect(!render->MatchesGeometry(changed),
           "every geometry incarnation prevents stale or ABA light binding");
    changed = geometry;
    changed.stamps[n].missingEvidence = MissingChunkEvidence::Unknown;
    Expect(!render->MatchesGeometry(changed),
           "absence knowledge participates in light binding compatibility");
    changed = geometry;
    changed.stamps[n].light += 100;
    changed.stamps[n].lightIncarnation += 200;
    Expect(render->MatchesGeometry(changed),
           "geometry provenance does not depend on baked light revisions");
  }
  auto wrong_world = geometry;
  wrong_world.world_geometry_identity = std::make_shared<const uint64_t>(0);
  Expect(!render->MatchesGeometry(wrong_world),
         "equal stamps cannot cross worlds");
  auto wrong_catalog = geometry;
  wrong_catalog.catalog = std::make_shared<BlockDefinitionCatalog>(*catalog);
  Expect(
      !render->MatchesGeometry(wrong_catalog),
      "catalog provenance is exact until geometry policy revisions are split");
  for (auto member :
       {&WorldVerticalProfile::MinY, &WorldVerticalProfile::MaxY,
        &WorldVerticalProfile::SeaLevelY, &WorldVerticalProfile::BedrockTopY,
        &WorldVerticalProfile::DatumY})
  {
    auto wrong_profile = geometry;
    ++(wrong_profile.profile.*member);
    Expect(!render->MatchesGeometry(wrong_profile),
           "all captured vertical profile inputs participate in compatibility");
  }
  auto duplicate = geometry.stamps;
  duplicate[1] = duplicate[2];
  Expect(!SectionGeometryRenderInputs::FromStamps(
              target, geometry.world_geometry_identity, geometry.profile,
              catalog, duplicate)
              .valid,
         "duplicate faces cannot supply a complete geometry input domain");
  auto permuted = geometry.stamps;
  std::swap(permuted[1], permuted[6]);
  Expect(render->MatchesGeometry(SectionGeometryRenderInputs::FromStamps(
             target, geometry.world_geometry_identity, geometry.profile,
             catalog, permuted)),
         "geometry input domains normalize face ordering");
  const auto field_identity = field->GetIncarnation();
  manager.EnsureChunk(target);
  Expect(!world.CaptureRenderLightSnapshot(target) &&
             render->MatchesGeometry(geometry),
         "geometry admission rejects fresh snapshot while retained predecessor "
         "stays immutable");
  Expect(manager.GetSectionLightField(target)->GetIncarnation() ==
                 field_identity &&
             !world.HasCompleteLightSamplingCalculation(target, catalog),
         "geometry admission shares field bytes but invalidates old absence "
         "receipt");
  const auto admitted = world.SampleSceneLight({16, -40, 8});
  Expect(admitted.state == SceneLightState::Transient && !admitted.IsReady() &&
             !admitted.packed && admitted.field_incarnation == field_identity,
         "geometry transition cannot expose stale AIR bytes as a ready sample");
  result = solve();
  manager.ReplaceComputedSectionLightVolume(
      target, *result.chunks[0].padded_light, true, true);
  Expect(world.RecordLightCalculation(result, catalog) == 1 &&
             world.HasCompleteLightSamplingCalculation(target, catalog),
         "new resident geometry receipt restores authority without a duplicate "
         "field");
  const auto resident_render = world.CaptureRenderLightSnapshot(target);
  Expect(resident_render &&
             !resident_render->MatchesGeometry(render->GeometryInputs()),
         "new resident certificate cannot bind retained absent geometry");
  world.SetBlock({15, -40, 8}, BLOCK_AIR);
  Expect(
      !world.HasCompleteLightSamplingCalculation(target, catalog),
      "source removal invalidates the independent field before recalculation");
  result = solve();
  manager.ReplaceComputedSectionLightVolume(
      target, *result.chunks[0].padded_light, true, true);
  Expect(world.RecordLightCalculation(result, catalog) == 1 &&
             UnpackBlock(field->GetData()[UChunk::LocalIndex({0, 8, 8})]) == 0,
         "source removal clears previously nonzero independent section light");
  const auto dark = world.SampleSceneLight({16, -40, 8});
  Expect(dark.state == SceneLightState::Computed && dark.IsReady() &&
             UnpackBlock(*dark.packed) == 0,
         "computed zero channel remains ready after source removal");
  const auto dark_render = world.CaptureRenderLightSnapshot(target);
  Expect(
      dark_render &&
          UnpackBlock(dark_render->Volume()
                          ->cells[SectionLightVolume::Index(0, 8, 8)]) == 0 &&
          UnpackBlock(
              render->Volume()->cells[SectionLightVolume::Index(0, 8, 8)]) ==
              14,
      "new zero channel field cannot mutate retained nonzero GPU upload bytes");
  UBlockWorld unknown;
  const auto missing_sample = unknown.SampleSceneLight({16, -40, 8});
  Expect(missing_sample.state == SceneLightState::Missing &&
             !missing_sample.IsReady() && !missing_sample.packed &&
             missing_sample.field_incarnation == 0,
         "unknown geometry does not imply daylight or a zero-valued field");
  unknown.SetLightingConfigurationIdentity(0);
  const auto still_missing = unknown.SampleSceneLight({16, -40, 8});
  Expect(!still_missing.IsReady(),
         "opaque configuration identity zero must not imply Flat");
  const auto flat =
      unknown.SampleSceneLight({16, -40, 8}, SceneLightPolicy::Flat);
  Expect(flat.state == SceneLightState::FlatConstant && flat.IsReady() &&
             *flat.packed == PackLight(15, 0) && flat.field_incarnation == 0 &&
             !unknown.GetCurrentLightSamplingVolume(target) &&
             !unknown.CaptureRenderLightSnapshot(target) &&
             unknown.GetChunkManager().GetSectionLightFieldCount() == 0,
         "explicit Flat constant allocates no field or physical receipt");
  unknown.SetVerticalProfile({-61, 317});
  const auto above = unknown.SampleSceneLight({-17, 318, 31});
  const auto below = unknown.SampleSceneLight({-17, -62, 31});
  Expect(
      above.state == SceneLightState::BoundaryConstant &&
          above.packed == PackLight(15, 0) &&
          below.state == SceneLightState::BoundaryConstant &&
          below.packed == PackLight(0, 0) &&
          !unknown.SampleSceneLight({-17, 317, 31}).IsReady() &&
          !unknown.SampleSceneLight({-17, -61, 31}).IsReady(),
      "boundary constants use exact profile cells, including partial slices");
  Expect(!InstallComputedLight(unknown, result.chunks[0], true, true) &&
             unknown.GetChunkManager().GetSectionLightFieldCount() == 0,
         "world installer cannot allocate a field from unknown absence");
  const auto missing =
      UChunkRelightSnapshot::CaptureSections(unknown, spec, {target})
          .Compute(registry);
  Expect(missing.chunks.empty() &&
             unknown.GetChunkManager().GetSectionLightFieldCount() == 0,
         "unknown absence cannot become an AIR calculation target or field");
  UBlockWorld diagonal;
  diagonal.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  diagonal.SetFluidDefinitions(definitions.get());
  const glm::ivec3 diagonal_target(1, -3, 1);
  diagonal.GetChunkManager().EnsureChunk(diagonal_target);
  diagonal.SetBlock({15, -40, 15}, 50);
  const auto solve_diagonal = [&]()
  {
    auto calculated = UChunkRelightSnapshot::CaptureSections(diagonal, spec,
                                                             {diagonal_target})
                          .Compute(registry);
    Expect(
        calculated.chunks.size() == 1 &&
            InstallComputedLight(diagonal, calculated.chunks[0], true, true) &&
            diagonal.RecordLightCalculation(calculated, catalog) == 1,
        "diagonal field installs with full physical provenance");
    return diagonal.CaptureRenderLightSnapshot(diagonal_target);
  };
  const auto diagonal_lit = solve_diagonal();
  diagonal.SetBlock({15, -40, 15}, BLOCK_AIR);
  Expect(!diagonal.CaptureRenderLightSnapshot(diagonal_target),
         "changed calculation input cannot supply a fresh render snapshot");
  const auto diagonal_dark = solve_diagonal();
  Expect(diagonal_lit && diagonal_dark &&
             diagonal_lit->MatchesGeometry(diagonal_dark->GeometryInputs()) &&
             diagonal_lit->FieldRevision() != diagonal_dark->FieldRevision() &&
             UnpackBlock(diagonal_lit->Volume()
                             ->cells[SectionLightVolume::Index(0, 8, 0)]) ==
                 13 &&
             UnpackBlock(diagonal_dark->Volume()
                             ->cells[SectionLightVolume::Index(0, 8, 0)]) == 0,
         "light-only change outside geometry domain permits a new field on "
         "unchanged geometry");
  UBlockWorld column;
  column.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  column.SetFluidDefinitions(definitions.get());
  column.SetBlock({15, -40, 8}, 50);
  spec.block_positions = {target * CHUNK_SIZE};
  spec.column_center_only = true;
  spec.include_known_air_outputs = false;
  spec.known_air_output_sections = {target};
  auto selected =
      UChunkRelightSnapshot::Capture(column, spec).Compute(registry);
  Expect(
      selected.chunks.size() == 1 && selected.chunks[0].coord == target &&
          UnpackBlock(
              selected.chunks[0].light_packed[UChunk::LocalIndex({0, 8, 8})]) ==
              14 &&
          InstallComputedLight(column, selected.chunks[0], true, true) &&
          column.RecordLightCalculation(selected, catalog) == 1 &&
          column.HasCompleteLightSamplingCalculation(target, catalog),
      "column job writes only explicitly demanded AIR and installs physical "
      "provenance");
  const glm::ivec3 other(1, -2, 0);
  auto malformed = selected.chunks[0];
  malformed.coord = other;
  malformed.light_packed[0] ^= 0x10;
  Expect(
      !InstallComputedLight(column, malformed, true, true) &&
          !column.GetChunkManager().GetSectionLightField(other),
      "inconsistent padded AIR output cannot allocate an independent resource");
  spec.known_air_output_sections.clear();
  Expect(UChunkRelightSnapshot::Capture(column, spec)
             .Compute(registry)
             .chunks.empty(),
         "knownAIR alone does not request whole-height column light fields");
  // An ordinary terrain band must not split all the explicit motion consumers
  // of the same column into separate worker jobs.
  column.GetChunkManager().EnsureChunk({1, 0, 0});
  column.GetChunkManager().EnsureChunk({1, -2, 0});
  spec.min_world_y = 0;
  spec.max_world_y = 15;
  spec.block_positions = {{16, 0, 0}};
  const glm::ivec3 predicted(1, -1, 0);
  spec.known_air_output_sections = {target, predicted, {1, -2, 0}, {1, -10, 0}};
  auto batched = UChunkRelightSnapshot::Capture(column, spec).Compute(registry);
  Expect(batched.inputs_complete && batched.chunks.size() == 3 &&
             std::any_of(batched.chunks.begin(), batched.chunks.end(),
                         [&](const auto &data)
                         { return data.coord == target; }) &&
             std::any_of(batched.chunks.begin(), batched.chunks.end(),
                         [&](const auto &data)
                         { return data.coord == predicted; }) &&
             std::any_of(batched.chunks.begin(), batched.chunks.end(),
                         [&](const auto &data)
                         { return data.coord == glm::ivec3(1, 0, 0); }),
         "one column job coalesces exact AIR consumers beyond terrain band");
  const auto geometry_count = column.GetChunkManager().GetResidentChunkCount();
  for (const auto &data : batched.chunks)
    InstallComputedLight(column, data, true, true);
  Expect(
      column.RecordLightCalculation(batched, catalog) == 3 &&
          column.SampleSceneLight({16, -40, 8}).IsReady() &&
          UnpackBlock(*column.SampleSceneLight({16, -40, 8}).packed) == 14 &&
          column.HasCompleteLightSamplingCalculation(predicted, catalog) &&
          column.GetChunkManager().GetResidentChunkCount() == geometry_count &&
          !column.HasCompleteLightSamplingCalculation({1, -2, 0}, catalog) &&
          !column.GetChunkManager().GetSectionLightField({1, -10, 0}),
      "batched AIR receipts keep nonzero values without writing intermediate "
      "resident or out-of-profile sections");
  column.GetChunkManager().EnsureChunk({1, 2, 0});
  auto *untouched = column.GetChunkManager().GetChunk({1, 2, 0});
  untouched->SetLightLocal({1, 2, 3}, 3, 4);
  const auto untouched_revision = untouched->GetLightFieldRevision();
  spec.light_sample_output_sections = {{1, -2, 0}, {1, -10, 0}};
  auto with_resident_sample =
      UChunkRelightSnapshot::Capture(column, spec).Compute(registry);
  Expect(with_resident_sample.inputs_complete &&
             with_resident_sample.chunks.size() == 4 &&
             std::any_of(with_resident_sample.chunks.begin(),
                         with_resident_sample.chunks.end(), [](const auto &data)
                         { return data.coord == glm::ivec3(1, -2, 0); }),
         "explicit resident sample outside terrain band joins same light job");
  const auto count_before_sample_install =
      column.GetChunkManager().GetResidentChunkCount();
  const auto geometry_before_sample_install =
      column.GetChunkManager().GetGeometryInputRevision();
  const auto sample_content_revision =
      column.GetChunkManager().GetChunk({1, -2, 0})->GetContentRevision();
  for (const auto &data : with_resident_sample.chunks)
    InstallComputedLight(column, data, true, true);
  Expect(
      column.RecordLightCalculation(with_resident_sample, catalog) == 4 &&
          column.SampleSceneLight({16, -30, 8}).IsReady() &&
          UnpackBlock(*column.SampleSceneLight({16, -30, 8}).packed) == 4 &&
          untouched->GetLightPackedLocal({1, 2, 3}) == PackLight(3, 4) &&
          untouched->GetLightFieldRevision() == untouched_revision &&
          !column.HasCompleteLightSamplingCalculation({1, 2, 0}, catalog) &&
          column.GetChunkManager().GetResidentChunkCount() ==
              count_before_sample_install &&
          column.GetChunkManager().GetGeometryInputRevision() ==
              geometry_before_sample_install &&
          column.GetChunkManager().GetChunk({1, -2, 0})->GetContentRevision() ==
              sample_content_revision,
      "resident sample obtains current nonzero proof without modifying "
      "unrequested resident light or fabricating geometry");
  UBlockWorld unknown_samples;
  const auto unknown_targets =
      UChunkRelightSnapshot::Capture(unknown_samples, spec).Compute(registry);
  Expect(!unknown_targets.inputs_complete && unknown_targets.chunks.empty() &&
             unknown_samples.GetChunkManager().GetSectionLightFieldCount() == 0,
         "explicit sample target cannot convert unknown absence into a field");
}

void TestWorldOwnedLightCertificates()
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
  auto catalog = registry.GetDefinitionsCatalogSnapshot();
  UBlockWorld world;
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  const glm::ivec3 coord(0, -3, 0);
  world.SetBlock({8, -40, 8}, 50);
  auto *chunk = world.GetChunkManager().GetChunk(coord);
  RelightJobSpec spec;
  spec.min_world_y = -64;
  spec.max_world_y = 319;
  auto compute = [&]()
  {
    return UChunkRelightSnapshot::CaptureSections(world, spec, {coord})
        .Compute(registry);
  };
  auto publish = [&](const RelightComputeResult &result)
  {
    for (const auto &data : result.chunks)
      InstallComputedLight(*world.GetChunkManager().GetChunk(data.coord), data,
                           result.include_skylight, result.include_block_light);
    return world.RecordLightCalculation(result, catalog);
  };
  auto result = compute();
  Expect(world.RecordLightCalculation(result, catalog) == 0,
         "calculation does not certify channels before output installation");
  InstallComputedLight(*chunk, result.chunks[0].light_packed, true, false);
  Expect(
      world.RecordLightCalculation(result, catalog) == 1 &&
          world.GetLightCalculationState(coord, LightDomain::Sky, catalog) ==
              LightCalculationState::Complete &&
          world.GetLightCalculationState(coord, LightDomain::Block, catalog) ==
              LightCalculationState::Missing &&
          !world.HasCompleteLightCalculation(coord, catalog),
      "sky-only installation cannot certify block light or whole-section "
      "completion");
  Expect(world.LightCalculationsStillCurrent({coord}, true, false, catalog) &&
             !world.LightCalculationsStillCurrent({coord}, true, true, catalog),
         "reuse checks only requested independently installed channels");
  Expect(publish(result) == 1 &&
             world.HasCompleteLightCalculation(coord, catalog),
         "installed known sky and block channels certify the section");
  Expect(world.HasCompleteLightSamplingCalculation(coord, catalog),
         "full installed padded output carries current physical authority");
  auto legacy_output = result;
  legacy_output.chunks[0].padded_light.reset();
  world.RecordLightCalculation(legacy_output, catalog);
  Expect(world.HasCompleteLightCalculation(coord, catalog) &&
             !world.HasCompleteLightSamplingCalculation(coord, catalog),
         "interior-only receipt cannot certify the mesh sampling footprint");
  Expect(!world.LightCalculationsStillCurrent({coord}, true, true, catalog, true),
         "sampling calculation reuse cannot skip an interior-only receipt");
  auto mismatched = result;
  auto wrong =
      std::make_shared<SectionLightVolume>(*result.chunks[0].padded_light);
  wrong->cells[SectionLightVolume::Index(17, 1, 1)] ^= 1;
  mismatched.chunks[0].padded_light = wrong;
  world.RecordLightCalculation(mismatched, catalog);
  Expect(!world.HasCompleteLightSamplingCalculation(coord, catalog),
         "matching interior does not hide an uninstalled padded sky value");
  world.RecordLightCalculation(result, catalog);
  Expect(world.HasCompleteLightSamplingCalculation(coord, catalog),
         "only matching padded bytes restore sampling authority");
  world.SetBlock({1024, -40, 1024}, 8);
  Expect(world.HasCompleteLightCalculation(coord, catalog),
         "unrelated remote mutation preserves exact dependency certificate");
  ChunkRenderDemandRecord legacy;
  legacy.world_epoch = 7;
  legacy.incarnation = chunk->GetIncarnation();
  legacy.has_settled_light = true;
  legacy.settled_content_rev = chunk->GetContentRevision();
  legacy.settled_light_rev = chunk->GetLightFieldRevision();
  Expect(LightSettlementMatchesCurrentCalculation(&legacy, chunk, 7, world,
                                                  catalog),
         "render gate admits current explicit complete calculation");
  const auto own_field = chunk->GetLightFieldRevision();
  world.SetBlock({16, -40, 8}, 8);
  Expect(
      own_field == chunk->GetLightFieldRevision() &&
          !world.HasCompleteLightCalculation(coord, catalog),
      "neighbor geometry invalidates certificate without own field mutation");
  Expect(LightSettlementMatchesCurrentChunk(&legacy, chunk, 7) &&
             !LightSettlementMatchesCurrentCalculation(&legacy, chunk, 7, world,
                                                       catalog),
         "own revision shortcut stays true after neighbor edit while "
         "production gate correctly closes");
  Expect(!world.LightCalculationsStillCurrent({coord}, true, true, catalog),
         "stale neighboring geometry prevents cached computation reuse");
  result = compute();
  publish(result);
  chunk->SetLightLocal({2, 3, 4}, chunk->GetSkyLightLocal({2, 3, 4}), 7);
  Expect(
      world.GetLightCalculationState(coord, LightDomain::Sky, catalog) ==
              LightCalculationState::Complete &&
          world.GetLightCalculationState(coord, LightDomain::Block, catalog) ==
              LightCalculationState::Missing,
      "independent block field write preserves current sky certificate only");
  result = compute();
  publish(result);
  UBlockWorld other;
  other.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  other.GetChunkManager() = std::move(world.GetChunkManager());
  Expect(ValidateRelightInputs(result, other, catalog) ==
             RelightInputValidation::Current,
         "transferred comparison world deliberately matches every captured "
         "input stamp");
  Expect(!other.HasCompleteLightCalculation(coord, catalog),
         "moving data and incarnation cannot transfer calculation authority to "
         "another world");
  world.GetChunkManager() = std::move(other.GetChunkManager());
  auto profile = world.GetVerticalProfile();
  auto changed_profile = profile;
  changed_profile.MaxY += 16;
  world.SetVerticalProfile(changed_profile);
  Expect(!world.HasCompleteLightCalculation(coord, catalog),
         "ceiling change invalidates all channel proofs");
  world.SetVerticalProfile(profile);
  world.SetLightingConfigurationIdentity(1);
  Expect(!world.HasCompleteLightCalculation(coord, catalog),
         "algorithm change invalidates proof");
  world.SetLightingConfigurationIdentity(0);
  auto other_catalog = std::make_shared<UBlockDefinitionStorage>();
  Expect(!world.HasCompleteLightCalculation(
             coord, other_catalog->GetCatalogSnapshot()),
         "material identity is part of calculation provenance");
  world.GetChunkManager().RemoveChunk(coord);
  world.GetChunkManager().EnsureChunk(coord);
  Expect(world.GetLightCalculationState(coord, LightDomain::Sky, catalog) ==
             LightCalculationState::Missing,
         "recycled section starts without inherited certificate");
  world.Clear();
  world.GetChunkManager().EnsureChunk(coord);
  Expect(!world.HasCompleteLightCalculation(coord, catalog),
         "world clear retires all proof ownership");

  UBlockWorld unknown;
  unknown.GetChunkManager().EnsureChunk(coord);
  auto lower = UChunkRelightSnapshot::CaptureSections(unknown, spec, {coord})
                   .Compute(registry);
  InstallComputedLight(*unknown.GetChunkManager().GetChunk(coord),
                       lower.chunks[0].light_packed, true, true);
  unknown.RecordLightCalculation(lower, catalog);
  Expect(unknown.GetLightCalculationState(coord, LightDomain::Sky, catalog) ==
                 LightCalculationState::Transient &&
             !unknown.HasCompleteLightCalculation(coord, catalog),
         "unknown input yields current transient output without completion");
  Expect(
      unknown.LightCalculationsStillCurrent({coord}, true, true, catalog),
      "unchanged transient result can be reused without claiming completion");
  unknown.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  Expect(unknown.GetLightCalculationState(coord, LightDomain::Sky, catalog) ==
             LightCalculationState::Missing,
         "new absence proof invalidates transient calculation without "
         "residency change");
  Expect(!unknown.LightCalculationsStillCurrent({coord}, true, true, catalog),
         "changed absence evidence forces recomputation of transient result");
  auto known = UChunkRelightSnapshot::CaptureSections(unknown, spec, {coord})
                   .Compute(registry);
  InstallComputedLight(*unknown.GetChunkManager().GetChunk(coord),
                       known.chunks[0].light_packed, true, true);
  unknown.RecordLightCalculation(known, catalog);
  Expect(unknown.HasCompleteLightCalculation(coord, catalog),
         "known replacement can finally certify the section");
  unknown.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return false; });
  Expect(!unknown.HasCompleteLightCalculation(coord, catalog),
         "withdrawn absence proof closes completion again");

  UBlockWorld dark;
  dark.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  dark.GetChunkManager().EnsureChunk(coord);
  for (int x = -16; x < 32; ++x)
    for (int z = -16; z < 32; ++z)
      dark.SetBlock({x, -32, z}, 8);
  auto zero = UChunkRelightSnapshot::CaptureSections(dark, spec, {coord})
                  .Compute(registry);
  InstallComputedLight(*dark.GetChunkManager().GetChunk(coord),
                       zero.chunks[0].light_packed, true, true);
  dark.RecordLightCalculation(zero, catalog);
  Expect(dark.GetChunkManager().GetChunk(coord)->GetLightFieldRevision() == 0 &&
             dark.HasCompleteLightCalculation(coord, catalog),
         "known completely dark zero-revision field has explicit complete "
         "provenance");
  dark.SetFluidDefinitions(definitions.get());
  Expect(!dark.SampleSceneLight(coord * CHUNK_SIZE + glm::ivec3(8)).IsReady(),
         "interior-only legacy receipt cannot authorize point sampling");
  InstallComputedLight(*dark.GetChunkManager().GetChunk(coord), zero.chunks[0],
                       true, true);
  dark.RecordLightCalculation(zero, catalog);
  const auto dark_sample =
      dark.SampleSceneLight(coord * CHUNK_SIZE + glm::ivec3(8));
  Expect(dark_sample.state == SceneLightState::Computed &&
             dark_sample.IsReady() && dark_sample.packed == 0 &&
             dark_sample.field_incarnation != 0,
         "fully certified zero byte is ready and distinct from missing light");
}

void TestBatchAbsenceEvidenceIsFreshAndExact()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;
  world.GetChunkManager().EnsureChunk({0, -3, 0});
  std::optional<int> floor = -4;
  int queries = 0;
  world.SetKnownAirColumnFloorQuery(
      [&](glm::ivec3)
      {
        ++queries;
        return floor;
      });
  RelightJobSpec spec;
  spec.min_world_y = -48;
  spec.max_world_y = -33;
  const auto captured =
      UChunkRelightSnapshot::CaptureSections(world, spec, {{0, -3, 0}})
          .Compute(registry);
  queries = 0;
  Expect(ValidateRelightInputs(captured, world,
                               registry.GetDefinitionsCatalogSnapshot(),
                               true) == RelightInputValidation::Current,
         "batched known-air column evidence preserves full strict validation");
  Expect(queries == 9 && captured.read_set.size() > 100,
         "one validation queries absence evidence once per column rather than "
         "once per vertical stamp");
  queries = 0;
  floor = 0;
  Expect(ValidateRelightInputs(captured, world,
                               registry.GetDefinitionsCatalogSnapshot()) ==
                 RelightInputValidation::ReadSet &&
             queries > 0,
         "next validation observes changed column evidence without a resident "
         "mutation");
  int arbitrary_queries = 0;
  world.SetKnownOpenSkyAbsentQuery(
      [&](glm::ivec3 c)
      {
        ++arbitrary_queries;
        return c.y % 2 == 0;
      });
  const auto arbitrary =
      UChunkRelightSnapshot::CaptureSections(world, spec, {{0, -3, 0}})
          .Compute(registry);
  arbitrary_queries = 0;
  Expect(ValidateRelightInputs(arbitrary, world,
                               registry.GetDefinitionsCatalogSnapshot()) ==
             RelightInputValidation::Current,
         "legacy arbitrary per-slice evidence remains exact");
  Expect(arbitrary_queries > 100, "nonmonotonic per-slice callback is never "
                                  "replaced by column-floor inference");
}

void TestGeometryInputMemoDoesNotReadUnrelatedLightFields()
{
  using namespace cutum;
  UBlockRegistry registry(nullptr, std::make_shared<UBlockDefinitionStorage>());
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  UBlockWorld world;
  world.SetKnownAirColumnFloorQuery(
      [](glm::ivec3) { return std::optional<int>(-4); },
      []() { return UBlockWorld::KnownAirEvidenceVersion{}; });
  const glm::ivec3 root(0), far(8, 0, 0);
  auto &manager = world.GetChunkManager();
  manager.EnsureChunk(root);
  manager.EnsureChunk(far);
  RelightJobSpec spec;
  spec.min_world_y = world.GetVerticalProfile().MinY;
  spec.max_world_y = world.GetVerticalProfile().MaxY;
  auto result = UChunkRelightSnapshot::CaptureSections(world, spec, {root})
                    .Compute(registry);
  InstallComputedLight(*manager.GetChunk(root), result.chunks[0].light_packed,
                       true, true);
  Expect(world.RecordLightCalculation(result, catalog) == 1 &&
             world.HasCompleteLightCalculation(root, catalog),
         "geometry-derived root proof primes validation memo");
  const auto walks =
      world.GetLightInputValidationStats().dependency_validations;
  const auto input_revision = manager.GetInputRevision();
  const auto geometry_revision = manager.GetGeometryInputRevision();
  manager.GetChunk(far)->SetLightLocal({0, 0, 0}, 3, 7);
  Expect(manager.GetInputRevision() > input_revision &&
             manager.GetGeometryInputRevision() == geometry_revision &&
             world.HasCompleteLightCalculation(root, catalog),
         "unrelated field write remains visible to light consumers without "
         "invalidating physical geometry input");
  Expect(world.GetLightInputValidationStats().dependency_validations == walks,
         "geometry-only context does not rewalk dependencies after light-only "
         "output writes");
  // Compatibility contexts may explicitly read light. They cannot use the
  // geometry-only memo, even though the geometry clock has not advanced.
  result.read_set.push_back(world.CaptureInputStamp(far, true, false));
  Expect(world.RecordLightCalculation(result, catalog) == 1 &&
             world.HasCompleteLightCalculation(root, catalog),
         "explicit light-read context is valid before source mutation");
  manager.GetChunk(far)->SetLightLocal({0, 0, 0}, 4, 7);
  Expect(!world.HasCompleteLightCalculation(root, catalog),
         "explicit light input mutation invalidates proof immediately");
}

void TestLightInputMemoRetainsResidentWorkingSet()
{
  using namespace cutum;
  UBlockRegistry registry(nullptr, std::make_shared<UBlockDefinitionStorage>());
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  UBlockWorld world;
  world.SetKnownAirColumnFloorQuery(
      [](glm::ivec3) { return std::optional<int>(-4); },
      []() { return UBlockWorld::KnownAirEvidenceVersion{}; });
  auto &manager = world.GetChunkManager();
  constexpr int count = 1100;
  for (int x = 0; x < count; ++x)
    manager.EnsureChunk({x, 0, 0});
  // Exercise provenance memo ownership, not the physical solver. Each record
  // has an independent immutable context for its already installed zero field.
  for (int x = 0; x < count; ++x)
  {
    const glm::ivec3 coord(x, 0, 0);
    RelightComputeResult result;
    result.input_catalog = catalog;
    result.input_profile = world.GetVerticalProfile();
    result.input_context_captured = true;
    result.inputs_complete = true;
    result.read_set.push_back(world.CaptureInputStamp(coord, false, true));
    result.chunks.push_back({coord, manager.GetChunk(coord)->GetLightData()});
    Expect(world.RecordLightCalculation(result, catalog) == 1,
           "independent resident context is recorded for both domains");
  }
  for (int x = 0; x < count; ++x)
    Expect(world.HasCompleteLightCalculation({x, 0, 0}, catalog),
           "large resident census primes current contexts");
  const auto walks =
      world.GetLightInputValidationStats().dependency_validations;
  for (int x = 0; x < count; ++x)
    Expect(world.HasCompleteLightCalculation({x, 0, 0}, catalog),
           "second stable census retains complete proofs");
  Expect(world.GetLightInputValidationStats().dependency_validations == walks,
         "stable resident working set above 1024 cannot evict itself on every "
         "census");
}

void TestVersionedLightInputMemoInvalidatesImmediately()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  UBlockWorld world;
  std::optional<int> floor = -4;
  UBlockWorld::KnownAirEvidenceVersion version;
  version.producers = {11, 0, 22, 0};
  version.min_y = -64;
  version.max_y = 319;
  std::string folder = "world_A";
  int evidence_queries = 0;
  bool publish_during_validation = false;
  world.SetKnownAirColumnFloorQuery(
      [&](glm::ivec3)
      {
        ++evidence_queries;
        if (publish_during_validation)
        {
          publish_during_validation = false;
          ++version.producers[1];
        }
        return floor;
      },
      [&]()
      {
        auto v = version;
        v.folder = folder;
        return v;
      });
  const glm::ivec3 root(0, -3, 0), neighbor(1, -3, 0);
  auto &manager = world.GetChunkManager();
  manager.EnsureChunk(root);
  manager.EnsureChunk(neighbor);
  auto *chunk = manager.GetChunk(root);
  auto *halo = manager.GetChunk(neighbor);
  RelightJobSpec spec;
  spec.min_world_y = -64;
  spec.max_world_y = 319;
  const auto refresh = [&]()
  {
    const auto result =
        UChunkRelightSnapshot::CaptureSections(world, spec, {root})
            .Compute(registry);
    InstallComputedLight(*chunk, result.chunks[0].light_packed, true, true);
    world.RecordLightCalculation(result, catalog);
    Expect(world.HasCompleteLightCalculation(root, catalog),
           "known geometry replacement certifies current field");
  };
  refresh();
  evidence_queries = 0;
  for (int n = 0; n < 200; ++n)
    Expect(world.HasCompleteLightCalculation(root, catalog),
           "stable certificate remains current");
  Expect(
      evidence_queries == 0,
      "unchanged authoritative revisions avoid all repeated dependency walks");
  ++version.producers[1];
  publish_during_validation = true;
  Expect(world.HasCompleteLightCalculation(root, catalog),
         "index publication during validation preserves truthful output");
  const int primed_queries = evidence_queries;
  Expect(world.HasCompleteLightCalculation(root, catalog) &&
             evidence_queries > primed_queries,
         "a validation overlapping new evidence cannot be cached across that "
         "publication");
  const int stable_queries = evidence_queries;
  Expect(world.HasCompleteLightCalculation(root, catalog) &&
             evidence_queries == stable_queries,
         "stable index revision admits reuse after a fresh validation");
  const auto before_self = manager.GetInputRevision();
  *chunk = *chunk;
  Expect(manager.GetInputRevision() == before_self &&
             world.HasCompleteLightCalculation(root, catalog),
         "self-assignment preserves current generation and input revision");
  const auto initial_revision = manager.GetInputRevision();
  halo->SetBlockLocal({0, 8, 8}, 8);
  Expect(manager.GetInputRevision() > initial_revision &&
             !world.HasCompleteLightCalculation(root, catalog),
         "direct neighbor chunk write invalidates memo before the next query "
         "in the same frame");
  refresh();
  const auto before_noop = manager.GetInputRevision();
  halo->SetBlockLocal({0, 8, 8}, 8);
  halo->ReplaceLightData(halo->GetLightData());
  Expect(manager.GetInputRevision() == before_noop,
         "unchanged voxel and light replacements do not churn input revision");
  const auto old_field = chunk->GetLightFieldRevision();
  UChunk copied = *halo;
  copied.SetBlockLocal({0, 8, 8}, BLOCK_AIR);
  Expect(manager.GetInputRevision() == before_noop,
         "unmanaged copied chunk cannot mutate the source world's clock");
  *halo = copied;
  Expect(manager.GetInputRevision() > before_noop &&
             chunk->GetLightFieldRevision() == old_field &&
             !world.HasCompleteLightCalculation(root, catalog),
         "copy assignment retains destination ownership and invalidates "
         "dependent field");
  refresh();
  UChunk same_revision_snapshot = *halo;
  halo->SetBlockLocal({2, 2, 2}, 8);
  refresh();
  same_revision_snapshot.SetBlockLocal({3, 3, 3}, 8);
  Expect(same_revision_snapshot.GetContentRevision() ==
             halo->GetContentRevision(),
         "comparison snapshots deliberately share a numeric content revision "
         "but differ in geometry");
  const auto previous_generation = halo->GetIncarnation();
  *halo = same_revision_snapshot;
  Expect(halo->GetIncarnation() != previous_generation &&
             !world.HasCompleteLightCalculation(root, catalog),
         "wholesale assignment retires generation even when copied content "
         "revision collides");
  refresh();
  UBlockWorld other;
  other.GetChunkManager().EnsureChunk(neighbor);
  const auto source_clock = manager.GetInputRevision();
  const auto target_clock = other.GetChunkManager().GetInputRevision();
  *other.GetChunkManager().GetChunk(neighbor) = *halo;
  Expect(manager.GetInputRevision() == source_clock &&
             other.GetChunkManager().GetInputRevision() > target_clock,
         "cross-world assignment notifies only the destination clock");
  const auto current_data = manager.GetInputRevision();
  floor = 0;
  ++version.producers[1];
  Expect(manager.GetInputRevision() == current_data &&
             !world.HasCompleteLightCalculation(root, catalog),
         "terrain evidence revision immediately invalidates a cached proof "
         "without resident mutation");
  floor = -4;
  ++version.producers[1];
  Expect(world.HasCompleteLightCalculation(root, catalog),
         "restored exact evidence can validate the original proof again");
  floor = 0;
  ++version.producers[3];
  Expect(!world.HasCompleteLightCalculation(root, catalog),
         "disk evidence revision invalidates cached proof");
  floor = -4;
  ++version.producers[3];
  Expect(world.HasCompleteLightCalculation(root, catalog),
         "disk evidence restoration is freshly checked");
  evidence_queries = 0;
  folder = "world_B";
  Expect(world.HasCompleteLightCalculation(root, catalog) &&
             evidence_queries == 9,
         "folder change triggers fresh validation even when producer counters "
         "match");
  evidence_queries = 0;
  ++version.producers[0];
  Expect(world.HasCompleteLightCalculation(root, catalog) &&
             evidence_queries == 9,
         "replacement terrain producer cannot reuse a previous instance's "
         "revision");
  const auto profile = world.GetVerticalProfile();
  auto extended = profile;
  extended.MaxY += 16;
  world.SetVerticalProfile(extended);
  Expect(!world.HasCompleteLightCalculation(root, catalog),
         "profile change invalidates positive cache entry");
  world.SetVerticalProfile(profile);
  world.SetLightingConfigurationIdentity(1);
  Expect(!world.HasCompleteLightCalculation(root, catalog),
         "mode change invalidates cached input context");
  world.SetLightingConfigurationIdentity(0);
  Expect(world.HasCompleteLightCalculation(root, catalog),
         "unchanged restored inputs can be checked again");
  auto foreign_catalog = std::make_shared<UBlockDefinitionStorage>();
  Expect(!world.HasCompleteLightCalculation(
             root, foreign_catalog->GetCatalogSnapshot()),
         "different catalog cannot use positive context memo");
  Expect(world.HasCompleteLightCalculation(root, catalog),
         "restoring pinned material identity triggers a current check");
  std::vector<UChunk> saved_chunks;
  manager.ForEachChunk([&](const UChunk &c) { saved_chunks.push_back(c); });
  UChunkManager replacement_manager;
  for (const auto &c : saved_chunks)
  {
    replacement_manager.EnsureChunk(c.GetCoord());
    *replacement_manager.GetChunk(c.GetCoord()) = c;
  }
  replacement_manager.GetChunk(neighbor)->SetBlockLocal({1, 1, 1}, 8);
  const uint64_t equal_revision =
      std::max(manager.GetInputRevision(),
               replacement_manager.GetInputRevision()) +
      16;
  const auto pad_revision = [&](UChunkManager &m)
  {
    m.EnsureChunk({777, 0, 777});
    auto *outside = m.GetChunk({777, 0, 777});
    while (m.GetInputRevision() < equal_revision)
      outside->SetBlockLocal(
          {0, 0, 0},
          outside->GetBlockLocal({0, 0, 0}) == BLOCK_AIR ? 8 : BLOCK_AIR);
  };
  pad_revision(manager);
  pad_revision(replacement_manager);
  Expect(world.HasCompleteLightCalculation(root, catalog),
         "unrelated padding mutation preserves the root's exact inputs");
  Expect(manager.GetInputRevisionIdentity() !=
             replacement_manager.GetInputRevisionIdentity(),
         "equal counter values belong to distinct manager identities");
  Expect(manager.GetInputRevision() == replacement_manager.GetInputRevision(),
         "comparison managers deliberately have equal numeric revisions");
  manager = std::move(replacement_manager);
  chunk = manager.GetChunk(root);
  halo = manager.GetChunk(neighbor);
  Expect(!world.HasCompleteLightCalculation(root, catalog),
         "replacement manager cannot inherit old proof at a matching numeric "
         "revision");
  refresh();
  manager.RemoveChunk(neighbor);
  Expect(!world.HasCompleteLightCalculation(root, catalog),
         "residency removal invalidates shared context memo");
  manager.EnsureChunk(neighbor);
  halo = manager.GetChunk(neighbor);
  Expect(!world.HasCompleteLightCalculation(root, catalog),
         "recycled equal air bytes have a different incarnation");
  refresh();
  halo->SetBlockLocal({0, 0, 0}, 8);
  refresh();
  const auto before_fluid = manager.GetInputRevision();
  const auto water = FluidCellState::Flowing(3).WithKind(FluidKind::Water);
  halo->SetFluidLocal({0, 0, 0}, water);
  Expect(manager.GetInputRevision() > before_fluid &&
             !world.HasCompleteLightCalculation(root, catalog),
         "direct fluid mutation alone invalidates geometry provenance");
  refresh();
  const auto fluid_noop = manager.GetInputRevision();
  halo->SetFluidLocal({0, 0, 0}, water);
  Expect(manager.GetInputRevision() == fluid_noop,
         "unchanged fluid state preserves memo input revision");
  halo->ClearFluidLocal({0, 0, 0});
  Expect(manager.GetInputRevision() > fluid_noop &&
             !world.HasCompleteLightCalculation(root, catalog),
         "clearing fluid alone invalidates current certificate");
  refresh();
  const auto before_light = manager.GetInputRevision();
  const auto before_light_geometry = manager.GetGeometryInputRevision();
  halo->SetLightLocal({0, 0, 0}, 5, 7);
  evidence_queries = 0;
  Expect(manager.GetInputRevision() > before_light &&
             manager.GetGeometryInputRevision() == before_light_geometry &&
             world.HasCompleteLightCalculation(root, catalog) &&
             evidence_queries == 0,
         "light writes notify input readers while geometry-only provenance "
         "retains its current validation");
  auto replacement = halo->GetLightData();
  replacement[0] = PackLight(9, 9);
  const auto before_bulk = manager.GetInputRevision();
  halo->ReplaceLightData(replacement);
  Expect(manager.GetInputRevision() > before_bulk &&
             world.HasCompleteLightCalculation(root, catalog),
         "bulk light replacement notifies input readers without invalidating "
         "geometry-only truth");
  const auto before_clear_light = manager.GetInputRevision();
  halo->ClearLightLocal({0, 0, 0});
  Expect(manager.GetInputRevision() > before_clear_light &&
             world.HasCompleteLightCalculation(root, catalog),
         "clearing light notifies current input identity");
  halo->ResetForReuse(neighbor);
  Expect(!world.HasCompleteLightCalculation(root, catalog),
         "direct reset changes neighbor generation before cached query");
  refresh();
  manager.EnsureChunk({-1, -3, 0});
  Expect(!world.HasCompleteLightCalculation(root, catalog),
         "appearance of a proven absent air section invalidates memo");
  refresh();
  UChunk relocated({123, 7, 456});
  relocated.SetBlockLocal({4, 5, 6}, 8);
  *halo = relocated;
  Expect(halo->GetCoord() == neighbor && halo->GetBlockLocal({4, 5, 6}) == 8 &&
             !world.HasCompleteLightCalculation(root, catalog),
         "bulk assignment preserves the managed coordinate and invalidates "
         "dependencies");
  refresh();
  world.SetKnownOpenSkyAbsentQuery(
      [&](glm::ivec3)
      {
        ++evidence_queries;
        return floor.has_value();
      });
  Expect(world.HasCompleteLightCalculation(root, catalog),
         "arbitrary callback clears the optional memo");
  floor.reset();
  Expect(
      !world.HasCompleteLightCalculation(root, catalog),
      "unversioned callbacks remain fresh without any revision notification");
  auto unidentified =
      UChunkRelightSnapshot::CaptureSections(world, spec, {root})
          .Compute(registry);
  unidentified.input_catalog.reset();
  unidentified.inputs_complete = false;
  InstallComputedLight(*chunk, unidentified.chunks[0].light_packed, true, true);
  world.RecordLightCalculation(unidentified, nullptr);
  Expect(!world.LightCalculationsStillCurrent({root}, true, true, nullptr),
         "missing immutable material identity never grants cached reuse of "
         "fallback physics");
}

void TestPendingCoverageRequiresCurrentKnownProof()
{
  using namespace cutum;
  PendingLightCoverage sparse;
  Expect(sparse.Request(-17, -17) == std::vector<int>{-2},
         "negative non-aligned note belongs to the lower signed section");
  sparse.Request(16, 16);
  Expect(!sparse.Contains(-1) && !sparse.Contains(0) && sparse.Size() == 2,
         "scheduling envelope does not invent obligations in its gaps");
  Expect(sparse.Request(-17, -17).empty(), "duplicate coverage is idempotent");
  Expect(sparse.Reconcile([](int) { return false; }) == 0 && sparse.Size() == 2,
         "missing proof cannot retire any obligation");

  Expect(!sparse.IsOwnedByBand(true, true, 79, 143),
         "a queued surface band cannot own negative section debt");
  Expect(!sparse.IsOwnedByBand(false, true, -64, 319),
         "a clippable queue admission is not durable coverage");
  Expect(!sparse.IsOwnedByBand(true, false, -64, 319),
         "an unspecified queue band cannot claim exact ownership");
  Expect(sparse.IsOwnedByBand(true, true, -32, 31),
         "an exact signed envelope owns all sparse obligations");
  sparse.Request(-64, -49);
  Expect(!sparse.IsOwnedByBand(true, true, -32, 31),
         "expanded underground debt invalidates old queue ownership");

  PendingLightCoverage incremental;
  auto add_unknown = [&](int min_y, int max_y)
  {
    return incremental.RequestIf(min_y, max_y, [](int cy) { return cy != -2; });
  };
  Expect(add_unknown(-32, -17).empty() && incremental.Empty(),
         "a repeated demand does not reopen an already proved section");
  Expect(add_unknown(-64, -1) == std::vector<int>({-4, -3, -1}) &&
             !incremental.Contains(-2),
         "new unknown debt stays exact around a proved interior section");

  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  UBlockWorld world;
  bool known = false;
  world.SetKnownOpenSkyAbsentQuery([&](glm::ivec3) { return known; });
  const glm::ivec3 upper(0, -3, 0), lower(0, -4, 0);
  world.GetChunkManager().EnsureChunk(upper);
  world.GetChunkManager().EnsureChunk(lower);
  RelightJobSpec spec;
  spec.min_world_y = -64;
  spec.max_world_y = 319;
  auto compute = [&](glm::ivec3 coord)
  {
    return UChunkRelightSnapshot::CaptureSections(world, spec, {coord})
        .Compute(registry);
  };
  auto install = [&](const RelightComputeResult &result)
  {
    bool changed = false;
    for (const auto &data : result.chunks)
      changed |= InstallComputedLight(
          *world.GetChunkManager().GetChunk(data.coord), data.light_packed,
          result.include_skylight, result.include_block_light);
    world.RecordLightCalculation(result, catalog);
    return changed;
  };
  PendingLightCoverage pending;
  pending.Request(-48, -33);
  const auto unknown = compute(upper);
  Expect(!install(unknown), "unknown lower-bound all-zero result is a no-op");
  auto reconcile = [&]
  {
    return pending.Reconcile(
        [&](int cy)
        { return world.HasCompleteLightCalculation({0, cy, 0}, catalog); });
  };
  Expect(reconcile() == 0 && pending.Contains(-3),
         "no-delta transient calculation preserves its obligation");
  known = true;
  auto older_job = compute(upper);
  // Extend the same column while a snapshot for the previous band is owned.
  pending.Request(-64, -33);
  install(older_job);
  Expect(reconcile() == 1 && pending.Size() == 1 && pending.Contains(-4),
         "older narrow result cannot close expanded uncovered section");
  Expect(pending.min_y == -64 && pending.max_y == -49,
         "remaining work keeps its exact negative-height scheduling envelope");
  install(compute(lower));
  Expect(reconcile() == 1 && pending.Empty(),
         "only known current calculations close both requested sections");
  pending.Request(-48, -33);
  world.SetBlock({16, -40, 8}, 8);
  Expect(reconcile() == 0 && pending.Contains(-3),
         "stale proof after a neighbor mutation cannot close reopened work");
  install(compute(upper));
  Expect(reconcile() == 1 && pending.Empty(),
         "fresh geometry-derived calculation completes reopened work");
}

} // namespace

int main(int argc, char **argv)
{
  if (argc == 2 && std::string(argv[1]) == "--pending-coverage")
  {
    TestPendingCoverageRequiresCurrentKnownProof();
    std::cout << "pending_light_coverage_test: OK\n";
    return 0;
  }
  if (argc == 2 && std::string(argv[1]) == "--light-proof")
  {
    TestGeometryInputMemoDoesNotReadUnrelatedLightFields();
    TestLightInputMemoRetainsResidentWorkingSet();
    TestVersionedLightInputMemoInvalidatesImmediately();
    TestKnownAirLightResourceCalculation();
    TestAbsentFieldInputStampIdentity();
    TestWorldOwnedLightCertificates();
    TestBatchAbsenceEvidenceIsFreshAndExact();
    std::cout << "light_calculation_certificate_test: OK\n";
    return 0;
  }
  if (argc == 2 && std::string(argv[1]) == "--geometry-halo")
  {
    TestSectionLightUsesKnownEmitterGeometry();
    TestGeometryHaloAgainstDenseReference();
    TestUnknownGeometryAndPinnedCatalog();
    TestClippedProfileDoesNotSeedOutOfWorldSource();
    std::cout << "section_light_geometry_test: OK\n";
    return 0;
  }
  TestPendingCoverageRequiresCurrentKnownProof();
  TestGeometryInputMemoDoesNotReadUnrelatedLightFields();
  TestLightInputMemoRetainsResidentWorkingSet();
  TestVersionedLightInputMemoInvalidatesImmediately();
  TestKnownAirLightResourceCalculation();
  TestAbsentFieldInputStampIdentity();
  TestWorldOwnedLightCertificates();
  TestBatchAbsenceEvidenceIsFreshAndExact();
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
  TestClippedProfileDoesNotSeedOutOfWorldSource();
  std::cout << "relight_result_install_test: OK\n";
}
