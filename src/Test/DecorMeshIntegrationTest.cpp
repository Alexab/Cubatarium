#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Jobs/PipelineAdmission.h"
#include "Render/Camera/Frustum.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Pipeline/GreedyDrawCategory.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkRelightSnapshot.h"

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
constexpr cutum::BlockId kCutoutLeaves = 633;

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

  cutum::BlockDefinition cutout_leaves;
  cutout_leaves.Name = "cutout_leaves";
  cutout_leaves.Physics = cutum::BlockPhysicsProfile::Solid();
  cutout_leaves.Physics.Movement.Occupancy = 0.0f;
  cutout_leaves.Render.Style = cutum::BlockRenderStyle::Cutout;

  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kFire] = fire;
  by_id[kTallGrass] = tall_grass;
  by_id[kCutoutLeaves] = cutout_leaves;

  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["fire"] = kFire;
  name_to_id["tall_grass"] = kTallGrass;
  name_to_id["cutout_leaves"] = kCutoutLeaves;

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

static const cutum::GreedyMeshBatch *FindGreedyBatchForBlock(
    const cutum::UChunkMeshCache &cache,
    const std::vector<cutum::GreedyBatchRef> &refs,
    cutum::BlockId block_id)
{
  for (const cutum::GreedyBatchRef &ref : refs)
  {
    const cutum::GreedyMeshBatch *batch = cache.TryGetGreedyBatch(ref);
    if (batch && batch->blockId == block_id)
    {
      return batch;
    }
  }
  return nullptr;
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

static void TestGreedyAlphaCutoutIsMaterialScoped(
    cutum::UBlockWorld &world, cutum::UBlockRegistry &registry)
{
  using cutum::ShouldUseGreedyAlphaCutout;
  Expect(!ShouldUseGreedyAlphaCutout(false, true, false),
         "opaque alpha discard must stay disabled for ordinary materials");
  Expect(ShouldUseGreedyAlphaCutout(false, false, true),
         "opaque alpha discard must follow the cutout material flag");
  Expect(ShouldUseGreedyAlphaCutout(true, true, false),
         "transparent passes must retain their pass-wide alpha policy");
  Expect(!ShouldUseGreedyAlphaCutout(true, false, true),
         "transparent passes must not inherit opaque material cutout state");

  const glm::ivec3 stone_pos(32, 10, 32);
  const glm::ivec3 leaves_pos(32, 11, 32);
  world.SetBlock(stone_pos, kStone);
  world.SetBlock(leaves_pos, kCutoutLeaves);

  cutum::UChunkMeshCache cache;
  cache.SetRenderSettings(MakeFrustumMeshSettings());
  cache.SetRenderDistanceChunks(4);
  RebuildAndCull(cache, world, registry,
                 glm::vec3(32.5f, 13.5f, 36.0f));

  const auto &opaque_refs = cache.GetGreedyOpaqueCutoutRefs();
  const auto *stone_batch =
      FindGreedyBatchForBlock(cache, opaque_refs, kStone);
  const auto *leaves_batch =
      FindGreedyBatchForBlock(cache, opaque_refs, kCutoutLeaves);
  Expect(stone_batch != nullptr && !stone_batch->AlphaCutout,
         "ordinary stone batches must not request alpha discard");
  Expect(leaves_batch != nullptr && leaves_batch->AlphaCutout,
         "cutout leaves batches must preserve their alpha discard flag");
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

static void TestOccludedMeshDependencyRepair(cutum::UBlockRegistry &registry)
{
  using namespace cutum;
  UBlockWorld world;
  const glm::ivec3 center(0, -3, 0);
  // A solid section with six known, opaque face neighbors publishes a valid
  // zero-quad result. Opening a neighbor later must make its wall visible.
  for (int x = -1; x <= CHUNK_SIZE; ++x)
    for (int y = -49; y <= -32; ++y)
      for (int z = -1; z <= CHUNK_SIZE; ++z)
        world.SetBlock({x, y, z}, kStone);
  UChunkMeshCache cache;
  cache.SetRenderSettings(MakeFrustumMeshSettings());
  cache.MarkDirty(center);
  cache.RebuildDirtyChunksWithStats(world, registry, 100, 100);
  Expect(cache.HasGreedyMesh(center) && !cache.HasDrawableGreedyMesh(center),
         "enclosed section publishes intentional zero-quad geometry");
  Expect(!cache.HasActiveBoundaryOverlay(center),
         "zero-quad regression must not rely on overlay debt");
  const glm::ivec3 opening(16, -40, 8);
  world.SetBlock(opening, BLOCK_AIR);
  cache.QueueMeshDependencyInvalidations(
      world, {UChunkManager::WorldToChunk(opening)}, "neighbor_geometry_edit");
  Expect(cache.HasPendingMeshDependencyInvalidation(center),
         "bulk dependency changes retain zero-quad section repair");
  cache.RebuildDirtyChunksWithStats(world, registry, 100, 100);
  Expect(cache.HasDrawableGreedyMesh(center),
         "neighbor opening reveals previously occluded section wall");
  world.SetBlock(opening, kStone);
  cache.QueueMeshDependencyInvalidation(center);
  cache.RebuildDirtyChunksWithStats(world, registry, 100, 100);
  Expect(!cache.HasDrawableGreedyMesh(center),
         "closing neighbor restores intentional zero-quad geometry");
  cache.QueueMeshDependencyInvalidation(center);
  Expect(cache.HasPendingMeshDependencyInvalidation(center),
         "single dependency entry point also retains zero-quad repair");
}

static void TestPublicationRefreshesDrawRefs(cutum::UBlockRegistry &registry)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetBlock({4, 4, 4}, kStone);
  UChunkMeshCache cache;
  auto settings = MakeFrustumMeshSettings();
  settings.FrustumCulling = false;
  cache.SetRenderSettings(settings);
  cache.RebuildAll(world, registry);
  const Frustum frustum{};
  const glm::mat4 view_projection(1.0f);
  const glm::vec3 eye(4.0f, 4.0f, 8.0f);
  cache.UpdateVisibleInstances(frustum, view_projection, eye);
  Expect(!cache.GetGreedyOpaqueCutoutRefs().empty(),
         "publication fixture must have a preceding live draw list");
  // A newly published material changes the draw ranges even when the same
  // section and GPU slot remain resident. There must be no timer during which
  // old ranges can be used with the replacement payload.
  world.SetBlock({6, 4, 4}, kFire);
  cache.MarkDirty({0, 0, 0});
  cache.RebuildDirtyChunksWithStats(world, registry, 100, 100);
  cache.UpdateVisibleInstances(frustum, view_projection, eye);
  Expect(CountGreedyVerticesForBlock(
             cache, cache.GetGreedyTransparentRefs(), kFire) > 0,
         "replacement publication immediately refreshes material draw refs");
}

static void TestFirstComputedLightPublication(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  enum class ProofCase
  {
    Current,
    InvalidatedInFlight,
    Uncertified
  };
  for (const auto proof_case :
       {ProofCase::Current, ProofCase::InvalidatedInFlight,
        ProofCase::Uncertified})
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.GetChunkManager().EnsureChunk({1, 0, 0});
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    const glm::ivec3 root(0);
    world.SetBlock({4, 4, 4}, kStone);
    world.SetBlock({15, 8, 8}, kStone);
    auto *chunk = world.GetChunkManager().GetChunk(root);
    RelightJobSpec spec;
    spec.min_world_y = world.GetVerticalProfile().MinY;
    spec.max_world_y = world.GetVerticalProfile().MaxY;
    const auto result =
        UChunkRelightSnapshot::CaptureSections(world, spec, {root})
            .Compute(registry);
    Expect(result.chunks.size() == 1 && result.inputs_complete,
           "first publication fixture has known physical inputs");
    chunk->ReplaceComputedLightVolume(*result.chunks[0].padded_light, true,
                                      true);
    Expect(chunk->GetLightFieldRevision() > 0,
           "nonzero field revision exercises unpublished first-light debt");
    const auto catalog = registry.GetDefinitionsCatalogSnapshot();
    if (proof_case != ProofCase::Uncertified)
      Expect(world.RecordLightCalculation(result, catalog) == 1,
             "first publication fixture installs physical calculation proof");

    if (proof_case != ProofCase::Uncertified)
    {
      const auto captured = ChunkMeshSnapshot::Capture(world, root, 1);
      Expect(captured.certifiedPaddedLight,
             "normal capture selects current padded physical output");
      auto *neighbor = world.GetChunkManager().GetChunk({1, 0, 0});
      neighbor->SetLightLocal({0, 8, 8}, 0, 15);
      const auto recaptured = ChunkMeshSnapshot::Capture(world, root, 1);
      Expect(captured.InputsStillValid(world) &&
                 recaptured.certifiedPaddedLight &&
                 recaptured.GetLightPacked({16, 8, 8}) ==
                     result.chunks[0]
                         .padded_light
                         ->cells[SectionLightVolume::Index(16, 8, 8)] &&
                 recaptured.GetLightPacked({16, 8, 8}) !=
                     neighbor->GetLightPackedLocal({0, 8, 8}),
             "poisoned neighbor light cannot alter certified capture or "
             "invalidate it");
      const auto direct = UGreedyMesher::BuildChunkMesh(world, root, registry);
      const auto immutable =
          UGreedyMesher::BuildChunkMesh(recaptured, registry);
      Expect(
          direct.size() == immutable.size(),
          "direct and immutable geometry agree with poisoned adjacent field");
      for (size_t i = 0; i < direct.size(); ++i)
        Expect(
            direct[i].LightPacked == immutable[i].LightPacked,
            "direct world face lighting uses same certified volume as capture");
    }

    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.AsyncMeshing = true;
    settings.FrustumCulling = false;
    cache.SetRenderSettings(settings);
    cache.SetMeshRebuildFocus(root, 4, 1);
    cache.MarkDirtyPriority(root);
    Expect(cache.GetLiveDirtyFirstMeshCount() == 1,
           "fixture owns an explicit first-mesh ticket");
    const auto geom = cache.GetChunkMeshRevision(root);
    const auto epoch = cache.GetCaptureStore().WorldEpoch();
    auto &demand = cache.GetRenderDemandStore();
    demand.NoteDemand(root, geom, chunk->GetLightFieldRevision(), 0, 1.0, epoch,
                      chunk->GetIncarnation());
    if (proof_case != ProofCase::Uncertified)
      demand.NoteLightCalculationSettled(root, epoch, chunk->GetIncarnation(),
                                         chunk->GetLightFieldRevision(),
                                         chunk->GetContentRevision());
    Expect(demand.Find(root)->published_light_rev == 0,
           "fixture has no predecessor publication");
    Expect(
        cache.GetCaptureStore().CaptureAndStore(world, root, geom).has_value(),
        "fixture captures exact current first-mesh inputs");
    const auto scheduled =
        cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
    if (scheduled.Scheduled != 1)
      std::cerr << "first-light fixture scheduled=" << scheduled.Scheduled
                << " dirty=" << cache.GetDirtyCount()
                << " first_cap=" << cache.GetLastFirstMeshScheduleEffectiveCap()
                << " skips=" << cache.GetLastMeshDirtyScheduleSkipN()
                << " snapshot_skips="
                << cache.GetLastMeshDirtyScheduleSkipSnapshotN()
                << " other_skips=" << cache.GetLastMeshDirtyScheduleSkipOtherN()
                << " capture=" << cache.HasPendingCaptureWork(root) << '\n';
    Expect(scheduled.Scheduled == 1 &&
               cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
           "one actual asynchronous first-mesh job completes");
    if (proof_case == ProofCase::InvalidatedInFlight)
      world.SetLightingConfigurationIdentity(
          2); // Full mode, different proof input.
    cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
    if (proof_case == ProofCase::InvalidatedInFlight)
    {
      Expect(!cache.HasDrawableGreedyMesh(root) && cache.HasPendingDirty(),
             "proof lost after capture cannot publish a normal first image or "
             "lose its retry obligation");
      continue;
    }
    Expect(cache.HasDrawableGreedyMesh(root),
           "first publication produces actual geometry");
    if (proof_case == ProofCase::Uncertified)
    {
      Expect(cache.HasProvisionalLightPreview(root),
             "computed-looking bytes without a proof remain explicit preview");
      continue;
    }
    Expect(!cache.HasProvisionalLightPreview(root) && !cache.HasPendingDirty(),
           "current computed field publishes once without preview/remesh "
           "bootstrap");
    Expect(cache.GetMeshedLightRevision(root) == chunk->GetLightFieldRevision(),
           "first publication carries the computed field revision");
    cache.UpdateVisibleInstances(Frustum{}, glm::mat4(1.0f), {4, 4, 8});
    const auto *batch = FindGreedyBatchForBlock(
        cache, cache.GetGreedyOpaqueCutoutRefs(), kStone);
    Expect(batch && !batch->vertices.empty(),
           "first publication has an inspectable draw payload");
    for (const auto &vertex : batch->vertices)
      Expect(vertex.lightPreview == 0.0f,
             "normal first publication does not retain encoded preview bits");
  }
}

static void TestWorldDemandIsolation()
{
  using namespace cutum;
  UChunkMeshCache first_world;
  UChunkMeshCache second_world;
  const glm::ivec3 coord(1, -3, 4);
  auto &first = first_world.GetRenderDemandStore();
  auto &second = second_world.GetRenderDemandStore();
  first.NoteDemand(coord, 7, 11, 0, 1.0, 1, 1);
  Expect(!second.Find(coord), "another world must not inherit section demand");
  second.NoteDemand(coord, 17, 19, 0, 1.0, 1, 1);
  first.NoteLightCalculationSettled(coord, 1, 1, 11, 7);
  Expect(!second.Find(coord)->has_settled_light,
         "another world's lighting proof must remain independent");
  Expect(first.Find(coord)->desired_geom_rev == 7,
         "identical coordinates and generation counters stay world-local");
  second.Clear();
  Expect(first.Find(coord) && first.Find(coord)->desired_light_rev == 11,
         "loading or clearing another world preserves the first world's work");
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
  TestGreedyAlphaCutoutIsMaterialScoped(world, registry);
  TestOverflowRetryWithNoDirtyAdmission(registry);
  TestOccludedMeshDependencyRepair(registry);
  TestPublicationRefreshesDrawRefs(registry);
  TestFirstComputedLightPublication(registry, definitions.get());
  TestWorldDemandIsolation();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
