#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Jobs/PipelineAdmission.h"
#include "Render/Camera/Frustum.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"

#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace
{

constexpr const char *kTestName = "decor_mesh_integration_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kFire = 631;
constexpr cutum::BlockId kTallGrass = 568;

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << kTestName << ": " << message << std::endl;
    std::exit(1);
  }
}

static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();

  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();

  cutum::BlockDefinition fire;
  fire.Name = "fire";
  fire.Physics = cutum::BlockPhysicsProfile::FromPreset("fire");
  fire.Render.Transparent = true;
  fire.Lighting.Emission = 14;

  cutum::BlockDefinition tall_grass;
  tall_grass.Name = "tall_grass";
  tall_grass.Physics = cutum::BlockPhysicsProfile::Solid();
  tall_grass.Physics.Movement.Occupancy = 0.0f;
  tall_grass.Render.Style = cutum::BlockRenderStyle::Cross;
  tall_grass.Render.Transparent = true;

  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kFire] = fire;
  by_id[kTallGrass] = tall_grass;

  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["fire"] = kFire;
  name_to_id["tall_grass"] = kTallGrass;

  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

static size_t
CountCrossInstances(const std::vector<cutum::CrossInstanceBatch> &batches)
{
  size_t count = 0;
  for (const cutum::CrossInstanceBatch &batch : batches)
  {
    count += batch.instances.size();
  }
  return count;
}

static size_t
CountGreedyVerticesForBlock(const cutum::UChunkMeshCache &cache,
                            const std::vector<cutum::GreedyBatchRef> &refs,
                            cutum::BlockId block_id)
{
  size_t count = 0;
  for (const cutum::GreedyBatchRef &ref : refs)
  {
    const cutum::GreedyMeshBatch *batch = cache.TryGetGreedyBatch(ref);
    if (!batch)
    {
      continue;
    }
    if (batch->blockId == block_id)
    {
      count += batch->vertices.size();
    }
  }
  return count;
}

static void RebuildAndCull(cutum::UChunkMeshCache &cache,
                           cutum::UBlockWorld &world,
                           cutum::UBlockRegistry &registry,
                           const glm::vec3 &camera_pos)
{
  cache.MarkAllDirtyFromWorld(world, true);
  const auto resident_count = cache.GetDirtyCount();
  Expect(resident_count > 0, "fixture must enqueue resident chunks");
  const glm::ivec3 absent_chunk(100, 100, 100);
  Expect(!world.GetChunkManager().HasChunk(absent_chunk),
         "ghost fixture must be absent");
  cache.MarkDirty(absent_chunk);
  const auto stats =
      cache.RebuildDirtyChunksWithStats(world, registry, 10000, 10000);
  Expect(cache.GetLastMeshDirtyPruneN() == 1, "prune only the absent chunk");
  Expect(static_cast<size_t>(stats.SyncRebuilt) == resident_count,
         "resident dirty chunks must survive pruning and rebuild");
  Expect(cache.GetGreedyCacheSize() > 0,
         "sync rebuild must publish resident chunks");
  cache.RebuildAll(world, registry);
  const glm::mat4 view =
      glm::lookAt(camera_pos, camera_pos + glm::vec3(0.0f, 0.0f, -1.0f),
                  glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::mat4 proj =
      glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 512.0f);
  const glm::mat4 vp = proj * view;
  cache.UpdateVisibleInstances(cutum::Frustum::FromViewProjection(vp), vp,
                               camera_pos);
}

static cutum::RenderSettings MakeFrustumMeshSettings()
{
  cutum::RenderSettings settings;
  settings.GreedyMeshing = true;
  settings.FaceQuads = true;
  settings.AsyncMeshing = false;
  settings.FrustumCulling = true;
  return settings;
}

static void TestCrossGrassVisibleWithFrustum(cutum::UBlockWorld &world,
                                             cutum::UBlockRegistry &registry)
{
  for (int x = 0; x < 4; ++x)
  {
    for (int z = 0; z < 4; ++z)
    {
      world.SetBlock(glm::ivec3(x, 10, z), kStone);
      world.SetBlock(glm::ivec3(x, 11, z), kTallGrass);
    }
  }

  cutum::UChunkMeshCache cache;
  cache.SetRenderSettings(MakeFrustumMeshSettings());
  cache.SetRenderDistanceChunks(4);

  const glm::vec3 camera_pos(2.0f, 12.5f, 6.0f);
  RebuildAndCull(cache, world, registry, camera_pos);

  const std::vector<cutum::CrossInstanceBatch> &cross_batches =
      cache.GetCrossBatches();
  if (cross_batches.empty())
  {
    std::cerr << "decor diagnostics: greedy_cache="
              << cache.GetGreedyCacheSize()
              << " vertices=" << cache.GetGreedyVertexCount()
              << " dirty=" << cache.GetDirtyCount() << '\n';
  }
  Expect(!cross_batches.empty(), "cross batches should not be empty");
  Expect(CountCrossInstances(cross_batches) >= 4,
         "cross grass instances should be present with frustum culling");
}

static void TestCampfireStoneMesh(cutum::UBlockWorld &world,
                                  cutum::UBlockRegistry &registry)
{
  const glm::ivec3 anchor(20, 11, 20);
  world.SetBlock(anchor + glm::ivec3(-1, 0, 0), kStone);
  world.SetBlock(anchor + glm::ivec3(0, 0, -1), kStone);
  world.SetBlock(anchor + glm::ivec3(0, 0, 1), kStone);
  world.SetBlock(anchor + glm::ivec3(1, 0, 0), kStone);
  world.SetBlock(anchor + glm::ivec3(0, 1, 0), kFire);

  cutum::UChunkMeshCache cache;
  cache.SetRenderSettings(MakeFrustumMeshSettings());
  cache.SetRenderDistanceChunks(4);

  const glm::vec3 camera_pos(glm::vec3(anchor) + glm::vec3(0.5f, 2.5f, 4.0f));
  RebuildAndCull(cache, world, registry, camera_pos);

  const auto &refs = cache.GetGreedyOpaqueCutoutRefs();
  Expect(CountGreedyVerticesForBlock(cache, refs, kStone) > 0,
         "campfire stone ring should produce opaque greedy vertices");
}

static void
TestOverflowRetryWithNoDirtyAdmission(cutum::UBlockRegistry &registry)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetBlock({1, 2, 3}, kStone);
  UChunk &chunk = *world.GetChunkManager().GetChunk({0, 0, 0});
  for (int y = 0; y < CHUNK_SIZE; ++y)
    for (int z = 0; z < CHUNK_SIZE; ++z)
      for (int x = 0; x < CHUNK_SIZE; ++x)
        chunk.SetLightLocal({x, y, z}, 15, 0);
  UChunkMeshCache cache;
  auto settings = MakeFrustumMeshSettings();
  cache.SetRenderSettings(settings);
  cache.RebuildAll(world, registry);
  const auto old_vertices = cache.GetGreedyVertexCount();
  Expect(old_vertices > 0, "retry fixture must have a visible predecessor");
  settings.AsyncMeshing = true;
  cache.SetRenderSettings(settings);
  world.SetBlock({6, 2, 3}, kStone);
  cache.MarkDirty({0, 0, 0});
  UPipelineAdmission::Get().SetResultCap(0);
  const auto scheduled =
      cache.RebuildDirtyChunksWithStats(world, registry, 0, 1);
  Expect(scheduled.Scheduled == 1, "schedule the remesh that will overflow");
  Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
         "overflow worker finishes");
  Expect(cache.GetMeshCompletedSize() == 0,
         "result budget rejection must exercise overflow");
  MeshWorkAdmission admission;
  admission.dirty_admit_budget = 0;
  cache.SetMeshWorkAdmission(admission);
  // Recovery of existing demand is not admission of a new edit. No new event
  // will arrive to repair a lit predecessor if overflow is silently forgotten.
  cache.RebuildDirtyChunksWithStats(world, registry, 0, 0);
  Expect(cache.GetDirtyCount() > 0 || cache.HasPendingAsyncMeshWork(),
         "overflow retry survives zero dirty admission with a visible "
         "predecessor");
  Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
         "retry worker finishes");
  UPipelineAdmission::Get().SetResultCap(128 * 1024 * 1024);
  for (int tick = 0; tick < 12; ++tick)
  {
    cache.RebuildDirtyChunksWithStats(world, registry, 8, 8);
    Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
           "retry converges");
  }
  Expect(cache.GetGreedyVertexCount() > old_vertices,
         "replacement eventually publishes");
  Expect(!cache.HasPendingDirty(), "retry leaves no orphan demand");
}

} // namespace

int main()
{
  const std::shared_ptr<cutum::UBlockDefinitionStorage> definitions =
      MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);

  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());

  TestCrossGrassVisibleWithFrustum(world, registry);
  TestCampfireStoneMesh(world, registry);
  TestOverflowRetryWithNoDirtyAdmission(registry);

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
