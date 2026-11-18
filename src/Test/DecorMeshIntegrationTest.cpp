#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Jobs/PipelineAdmission.h"
#include "Render/Camera/Frustum.h"
#include "Render/Lighting/SectionRenderLightBindings.h"
#if defined(SECTION_LIGHT_BINDING_DRIVER)
#include "Render/GlIncludes.h"
#include "Render/Lighting/SectionLightBindingTable.h"
#include <GLFW/glfw3.h>
#endif
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Mesh/GreedyMesher.h"
#include "Render/Pipeline/GreedyDrawCategory.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkRelightSnapshot.h"

#include <algorithm>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace cutum
{
struct ChunkMeshCaptureAdmissionTestAccess
{
  static bool CommitPacked(UChunkMeshCache &cache, const UBlockWorld &world,
                           UBlockRegistry &registry,
                           const ChunkMeshSnapshot &snapshot)
  {
    GpuMeshProcessResult result;
    result.quadCount = 1;
    result.hasLitDrawableFace = true;
    return cache.CommitGpuMeshResult(
        world, registry, snapshot.coord,
        cache.GetChunkMeshRevision(snapshot.coord), std::move(result), {},
        false, snapshot.inputStamps[0].light, true, {}, false, {},
        &snapshot.inputStamps,
        snapshot.GeometryRenderInputs(registry.GetDefinitionsCatalogSnapshot()),
        snapshot.renderLight);
  }
  static void ReserveOneRepair(UChunkMeshCache &cache)
  {
    cache.CaptureRefreshBudgetLeft = 0;
    cache.FirstMeshCaptureReserveLeft = 0;
    cache.LightRepairCaptureReserveLeft = 1;
  }
  static bool Acquire(UChunkMeshCache &cache, const UBlockWorld &world,
                      UBlockRegistry &registry, glm::ivec3 coord)
  {
    const auto result = cache.TryAcquireSnapshotForSchedule(
        world, registry, coord, cache.GetChunkMeshRevision(coord));
    return result.kind == UChunkMeshCache::SnapshotAcquireKind::Ready &&
           result.snapshot.has_value();
  }
};
} // namespace cutum

namespace
{

constexpr const char *kTestName = "decor_mesh_integration_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kFire = 631;
constexpr cutum::BlockId kTallGrass = 568;
constexpr cutum::BlockId kCutoutLeaves = 633;
constexpr cutum::BlockId kSolidGlass = 634;

// Only a lifetime/publication test double. The GPU driver's separate tests
// exercise actual allocation, uploads, shader reads and fences.
class TestRenderLightBinding final : public cutum::SectionRenderLightBinding
{
public:
  TestRenderLightBinding(uint64_t epoch,
                         const cutum::SectionGeometryRenderInputs &geometry,
                         const cutum::SectionRenderLightSnapshot &light)
      : SectionRenderLightBinding(epoch, geometry, light)
  {
  }
  std::array<uint32_t, 4> ShaderWords() const override { return {7, 0, 1, 0}; }
  bool IsLive() const override { return live; }
  bool live{true};
};

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << kTestName << ": " << message << std::endl;
    std::exit(1);
  }
}

#if defined(SECTION_LIGHT_BINDING_DRIVER)
static void ProbeCertifiedBinding(
    cutum::SectionRenderLightBindings &store,
    const std::shared_ptr<cutum::SectionRenderLightBinding> &binding)
{
  const auto compile = [](GLenum type, const std::string &source)
  {
    const auto shader = glCreateShader(type);
    const char *text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
      char log[2048]{};
      glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
      std::cerr << log << '\n';
    }
    Expect(ok != 0, "certified binding readback shader compiles");
    return shader;
  };
  const auto vertex =
      compile(GL_VERTEX_SHADER,
              "#version 330 core\n"
              "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
              "gl_Position=vec4(p*2.0-1.0,0,1);}");
  const auto fragment =
      compile(GL_FRAGMENT_SHADER,
              std::string("#version 330 core\n") +
                  cutum::SectionLightBindingTable::ShaderInterface() +
                  "uniform uvec4 handle; uniform ivec3 origin;"
                  "layout(location=0) out uint value;"
                  "void main(){ivec2 p=ivec2(gl_FragCoord.xy);uint packed;"
                  "bool ok=sectionLightFetch(handle,origin+"
                  "ivec3(p.x-2,p.y%20-2,p.y/20-2),packed);"
                  "value=ok?packed:256u;}");
  const auto program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  Expect(linked != 0, "certified binding readback program links");
  GLuint vao = 0, texture = 0, framebuffer = 0;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 20, 400, 0, GL_RED_INTEGER,
               GL_UNSIGNED_INT, nullptr);
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  Expect(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
         "certified binding readback framebuffer complete");
  Expect(store.Bind(7, 8), "certified field/table resources bind");
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "uSectionLightAtlas"), 7);
  glUniform1i(glGetUniformLocation(program, "uSectionLightBindings"), 8);
  const auto incarnation = store.ShaderIncarnation();
  const auto words = binding->ShaderWords();
  const auto origin = binding->GeometryInputs().coord * cutum::CHUNK_SIZE;
  glUniform2uiv(glGetUniformLocation(program, "uSectionLightTableIncarnation"),
                1, incarnation.data());
  glUniform4uiv(glGetUniformLocation(program, "handle"), 1, words.data());
  glUniform3i(glGetUniformLocation(program, "origin"), origin.x, origin.y,
              origin.z);
  glViewport(0, 0, 20, 400);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  std::array<uint32_t, cutum::SectionLightVolume::CellCount> pixels{};
  glReadPixels(0, 0, 20, 400, GL_RED_INTEGER, GL_UNSIGNED_INT, pixels.data());
  const auto &expected = binding->LightSnapshot().Volume()->cells;
  for (size_t n = 0; n < pixels.size(); ++n)
    Expect(
        pixels[n] == expected[n],
        "published GPU binding reads every certified physical sky/block cell");
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glUseProgram(0);
  glBindVertexArray(0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(1, &texture);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  Expect(glGetError() == GL_NO_ERROR,
         "certified GPU field readback has no GL errors");
}
#endif

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

  cutum::BlockDefinition solid_glass;
  solid_glass.Name = "solid_glass";
  solid_glass.Physics = cutum::BlockPhysicsProfile::Solid();
  solid_glass.Render.Transparent = true;

  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kFire] = fire;
  by_id[kTallGrass] = tall_grass;
  by_id[kCutoutLeaves] = cutout_leaves;
  by_id[kSolidGlass] = solid_glass;

  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["fire"] = kFire;
  name_to_id["tall_grass"] = kTallGrass;
  name_to_id["cutout_leaves"] = kCutoutLeaves;
  name_to_id["solid_glass"] = kSolidGlass;

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

static void
TestNoSurfaceGeometryProof(cutum::UBlockRegistry &registry,
                           const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  const glm::ivec3 center(0, -3, 0);
  for (int x = -1; x <= CHUNK_SIZE; ++x)
    for (int y = -49; y <= -32; ++y)
      for (int z = -1; z <= CHUNK_SIZE; ++z)
        world.SetBlock({x, y, z}, kStone);
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  const auto classify = [&]()
  { return UGreedyMesher::CaptureNoSurface(world, center, registry, catalog); };
  const auto proof = classify();
  Expect(proof.has_value(),
         "enclosed geometry has a NoSurface proof before light calculation");
  Expect(!world.HasCompleteLightSamplingCalculation(center, catalog),
         "NoSurface does not synthesize a physical light calculation");
  Expect(proof->Matches(world.CaptureGeometryRenderInputs(center, catalog)),
         "NoSurface carries current complete geometry inputs");
  world.GetChunkManager().GetChunk(center)->SetLightLocal({8, 8, 8}, 15, 15);
  Expect(proof->Matches(world.CaptureGeometryRenderInputs(center, catalog)),
         "light bytes do not invalidate geometry-only proof");
  const glm::ivec3 opening(16, -40, 8);
  world.SetBlock(opening, BLOCK_AIR);
  Expect(!proof->Matches(world.CaptureGeometryRenderInputs(center, catalog)) &&
             !classify(),
         "neighbor opening invalidates NoSurface and requires surface "
         "publication");
  Expect(!UGreedyMesher::BuildChunkMesh(world, center, registry, catalog.get())
              .empty(),
         "independent actual mesher emits the exposed neighbor wall");
  world.SetBlock(opening, kStone);
  Expect(classify().has_value(), "closing neighbor restores geometry proof");
  const glm::ivec3 interior(8, -40, 8);
  for (BlockId id : {kTallGrass, kCutoutLeaves, BlockId(65000)})
  {
    world.SetBlock(interior, id);
    Expect(!classify(),
           "sprites, cutouts and unknown materials cannot certify NoSurface");
  }
  world.SetBlock(interior, BLOCK_AIR);
  Expect(!classify(),
         "interior cavity has visible walls even with closed outer boundary");
  world.SetFluidState(interior,
                      FluidCellState::Source().WithKind(FluidKind::Water));
  Expect(!classify(), "active fluid in AIR cannot certify NoSurface");
  world.SetFluidState(interior, FluidCellState{});
  world.SetBlock(interior, kStone);
  const auto current = classify();
  Expect(current.has_value(), "restored solid section is certified");
  auto other_catalog = std::make_shared<BlockDefinitionCatalog>(*catalog);
  Expect(!current->Matches(
             world.CaptureGeometryRenderInputs(center, other_catalog)),
         "catalog identity invalidates retained NoSurface evidence");
  UBlockWorld other;
  Expect(!current->Matches(other.CaptureGeometryRenderInputs(center, catalog)),
         "another world cannot reuse the proof");
  const auto profile = world.GetVerticalProfile();
  for (int component = 0; component < 5; ++component)
  {
    auto changed = profile;
    switch (component)
    {
    case 0:
      --changed.MinY;
      break;
    case 1:
      ++changed.MaxY;
      break;
    case 2:
      ++changed.SeaLevelY;
      break;
    case 3:
      ++changed.BedrockTopY;
      break;
    case 4:
      ++changed.DatumY;
      break;
    }
    world.SetVerticalProfile(changed);
    Expect(
        !current->Matches(world.CaptureGeometryRenderInputs(center, catalog)),
        "each vertical profile component invalidates retained evidence");
  }
  world.SetVerticalProfile(profile);
  world.GetChunkManager().RemoveChunk({1, -3, 0});
  Expect(!classify(),
         "unknown face neighbor cannot masquerade as occluded geometry");
  Expect(!current->Matches(world.CaptureGeometryRenderInputs(center, catalog)),
         "neighbor unload invalidates retained geometry evidence");
  for (int y = -48; y <= -33; ++y)
    for (int z = 0; z < CHUNK_SIZE; ++z)
      world.SetBlock({16, y, z}, kStone);
  Expect(
      classify().has_value() &&
          !current->Matches(world.CaptureGeometryRenderInputs(center, catalog)),
      "same-coordinate neighbor reload produces fresh evidence");
  for (int y = -48; y <= -33; ++y)
    for (int z = 0; z < CHUNK_SIZE; ++z)
      for (int x = 0; x < CHUNK_SIZE; ++x)
        world.SetBlock({x, y, z}, BLOCK_AIR);
  Expect(classify().has_value(),
         "resident AIR proves no draw before calculating light");
  world.SetFluidState(interior,
                      FluidCellState::Source().WithKind(FluidKind::Water));
  Expect(!classify(), "fluid rejects a previously certified AIR-only section");
}

static void
TestSecondPlaneGeometryParity(cutum::UBlockRegistry &registry,
                              const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  int mismatches = 0;
  for (int axis = 0; axis < 3; ++axis)
    for (int sign : {-1, 1})
    {
      UBlockWorld world;
      world.SetFluidDefinitions(definitions);
      world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      const glm::ivec3 coord(0, -3, 0);
      const auto origin = coord * CHUNK_SIZE;
      glm::ivec3 local(8), direction(0);
      local[axis] = sign < 0 ? 0 : CHUNK_SIZE - 1;
      direction[axis] = sign;
      const auto pos = origin + local;
      const auto beyond = pos + 2 * direction;
      world.SetBlock(pos, kStone);
      world.SetBlock(pos + direction, kSolidGlass);
      world.SetBlock(beyond, kStone);
      const auto catalog = registry.GetDefinitionsCatalogSnapshot();
      const auto face_count = [&](const std::vector<GreedyQuad> &quads)
      {
        return std::count_if(quads.begin(), quads.end(),
                             [&](const GreedyQuad &q)
                             {
                               return q.Id == kStone && q.axis == axis &&
                                      q.faceSign == sign &&
                                      q.slice == local[axis];
                             });
      };
      const auto live =
          UGreedyMesher::BuildChunkMesh(world, coord, registry, catalog.get());
      Expect(face_count(live) == 1,
             "live mesher emits thin-wall face against backed solid glass");
      const auto snapshot = ChunkMeshSnapshot::Capture(world, coord, 0);
      const auto captured =
          UGreedyMesher::BuildChunkMesh(snapshot, registry, catalog.get());
      if (face_count(captured) != face_count(live))
      {
        ++mismatches;
        std::cerr << "second-plane face mismatch axis=" << axis
                  << " sign=" << sign << " live=" << face_count(live)
                  << " snapshot=" << face_count(captured) << '\n';
        continue;
      }
      Expect(snapshot.GetBlock(beyond) == kStone &&
                 snapshot.GetBlockIgnoringOverlay(beyond) == kStone,
             "both snapshot geometry readers expose the captured second plane");
      Expect(snapshot.InputsStillValid(world),
             "unmodified second-plane snapshot is current");
      auto overlay = snapshot;
      BoundaryOverlaySetMissingFaces(
          overlay.boundaryOverlay,
          static_cast<uint8_t>(1u << (axis * 2 + (sign > 0 ? 1 : 0))));
      Expect(overlay.GetBlock(beyond) == BLOCK_AIR &&
                 overlay.GetBlockIgnoringOverlay(beyond) == kStone,
             "second-plane geometry respects the same face overlay as its "
             "adjacent plane");
      auto edge = beyond;
      edge[(axis + 1) % 3] = origin[(axis + 1) % 3] - 1;
      Expect(
          snapshot.GetBlockIgnoringOverlay(edge) == BLOCK_AIR &&
              snapshot.GetBlockIgnoringOverlay(pos + 3 * direction) ==
                  BLOCK_AIR,
          "axial second-plane access never wraps edges or farther coordinates");
      world.SetBlock(beyond, BLOCK_AIR);
      Expect(snapshot.GetBlock(beyond) == kStone &&
                 !snapshot.InputsStillValid(world),
             "second-plane edit invalidates provenance without changing "
             "retained bytes");
      const auto opened = ChunkMeshSnapshot::Capture(world, coord, 1);
      Expect(face_count(UGreedyMesher::BuildChunkMesh(world, coord, registry,
                                                      catalog.get())) == 0 &&
                 face_count(UGreedyMesher::BuildChunkMesh(opened, registry,
                                                          catalog.get())) == 0,
             "fresh live and snapshot meshes agree after second-plane opening");
    }
  Expect(mismatches == 0, "all six section seams preserve the live mesher "
                          "second-plane face decision");
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
  Expect(CountGreedyVerticesForBlock(cache, cache.GetGreedyTransparentRefs(),
                                     kFire) > 0,
         "replacement publication immediately refreshes material draw refs");
}

static void TestFirstComputedLightPublication(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions,
    bool use_gpu_bindings = false)
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

    SectionRenderLightBindings gpu_bindings;
    UChunkMeshCache cache;
    if (use_gpu_bindings && proof_case == ProofCase::Current)
    {
      const auto source = world.CaptureRenderLightSnapshot(root);
      Expect(
          source && gpu_bindings.BeginWorld(
                        cache.GetCaptureStore().WorldEpoch(),
                        source->GeometryInputs().world_geometry_identity, 16,
                        8u * SectionLightVolume::CellCount, 8),
          "actual certified GPU resource store initializes in current world");
    }
    bool reject_resource = false;
    int prepare_calls = 0;
    if (proof_case == ProofCase::Current)
      cache.SetPrepareRenderLightFn(
          [&](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
              const SectionRenderLightSnapshot &light)
              -> std::shared_ptr<SectionRenderLightBinding>
          {
            ++prepare_calls;
            if (reject_resource)
              return {};
            if (use_gpu_bindings)
              return gpu_bindings.Prepare(epoch, geometry, light);
            return std::make_shared<TestRenderLightBinding>(epoch, geometry,
                                                            light);
          });
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
      Expect(!cache.GetPublishedRenderLightSnapshot(root),
             "preview publication cannot invent a certified physical field "
             "snapshot");
      continue;
    }
    Expect(!cache.HasProvisionalLightPreview(root) && !cache.HasPendingDirty(),
           "current computed field publishes once without preview/remesh "
           "bootstrap");
    Expect(cache.GetMeshedLightRevision(root) == chunk->GetLightFieldRevision(),
           "first publication carries the computed field revision");
    const auto published_geometry =
        cache.GetPublishedGeometryRenderInputs(root);
    const auto physical_light = world.CaptureRenderLightSnapshot(root);
    const auto published_light = cache.GetPublishedRenderLightSnapshot(root);
    Expect(published_geometry && physical_light && published_light &&
               physical_light->MatchesGeometry(*published_geometry) &&
               published_light->MatchesGeometry(*published_geometry) &&
               published_light->FieldIncarnation() ==
                   physical_light->FieldIncarnation() &&
               published_light->FieldRevision() ==
                   physical_light->FieldRevision(),
           "actual asynchronous publication retains compatible "
           "geometry/catalog/profile provenance");
    SectionRenderLightBindings sync_gpu_bindings;
    UChunkMeshCache sync_cache;
    auto sync_settings = settings;
    sync_settings.AsyncMeshing = false;
    sync_cache.SetRenderSettings(sync_settings);
    sync_cache.RebuildAll(world, registry);
    if (use_gpu_bindings)
      Expect(sync_gpu_bindings.BeginWorld(
                 sync_cache.GetCaptureStore().WorldEpoch(),
                 published_geometry->world_geometry_identity, 16,
                 8u * SectionLightVolume::CellCount, 8),
             "synchronous cache owns its independent capture epoch GPU scope");
    sync_cache.SetPrepareRenderLightFn(
        [&](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
            const SectionRenderLightSnapshot &light)
            -> std::shared_ptr<SectionRenderLightBinding>
        {
          if (use_gpu_bindings)
            return sync_gpu_bindings.Prepare(epoch, geometry, light);
          return std::make_shared<TestRenderLightBinding>(epoch, geometry,
                                                          light);
        });
    sync_cache.RebuildChunkImmediate(world, registry, root);
    const auto sync_geometry =
        sync_cache.GetPublishedGeometryRenderInputs(root);
    const auto sync_light = sync_cache.GetPublishedRenderLightSnapshot(root);
    Expect(
        sync_geometry && sync_light &&
            physical_light->MatchesGeometry(*sync_geometry) &&
            sync_light->MatchesGeometry(*sync_geometry),
        "actual synchronous publication retains compatible source provenance");
    cache.UpdateVisibleInstances(Frustum{}, glm::mat4(1.0f), {4, 4, 8});
    const auto *batch = FindGreedyBatchForBlock(
        cache, cache.GetGreedyOpaqueCutoutRefs(), kStone);
    Expect(batch && !batch->vertices.empty(),
           "first publication has an inspectable draw payload");
    for (const auto &vertex : batch->vertices)
      Expect(vertex.lightPreview == 0.0f,
             "normal first publication does not retain encoded preview bits");
    const auto binding = cache.GetPublishedRenderLightBinding(root);
    Expect(binding &&
               binding->Matches(epoch, *published_geometry, *published_light) &&
               batch->renderLightBinding == binding,
           "actual async publication and drawable batch own compatible "
           "resource lease");
    const auto published_count = cache.GetPublishCount(root);
    const auto previous_vertices = cache.GetGreedyVertexCount();
    // Create real unsatisfied geometry debt. A remesh of identical already
    // published inputs may legitimately be elided by the scheduler.
    world.SetBlock({6, 4, 4}, kStone);
    const auto replacement_result =
        UChunkRelightSnapshot::CaptureSections(world, spec, {root})
            .Compute(registry);
    Expect(replacement_result.inputs_complete &&
               replacement_result.chunks.size() == 1,
           "resource rejection fixture has fresh calculated physical inputs");
    chunk->ReplaceComputedLightVolume(
        *replacement_result.chunks[0].padded_light, true, true);
    Expect(world.RecordLightCalculation(replacement_result, catalog) == 1,
           "geometry edit installs fresh canonical calculation proof");
    cache.MarkDirtyPriority(root);
    demand.NoteDemand(root, cache.GetChunkMeshRevision(root),
                      chunk->GetLightFieldRevision(), 0, 1.0, epoch,
                      chunk->GetIncarnation());
    demand.NoteLightCalculationSettled(root, epoch, chunk->GetIncarnation(),
                                       chunk->GetLightFieldRevision(),
                                       chunk->GetContentRevision());
    reject_resource = true;
    const auto snapshot = ChunkMeshSnapshot::Capture(
        world, root, cache.GetChunkMeshRevision(root));
    const int before_packed_prepare = prepare_calls;
    Expect(!ChunkMeshCaptureAdmissionTestAccess::CommitPacked(
               cache, world, registry, snapshot) &&
               cache.GetPublishCount(root) == published_count &&
               cache.GetPublishedRenderLightBinding(root) == binding &&
               cache.GetGreedyVertexCount() == previous_vertices,
           "packed commit cannot retire predecessor when resource preparation "
           "fails");
    Expect(prepare_calls == before_packed_prepare + 1,
           "packed failure reaches actual resource preparation callback");
    cache.MarkDirtyPriority(root);
    cache.GetCaptureStore().Invalidate(root);
    Expect(cache.GetCaptureStore()
               .CaptureAndStore(world, root, cache.GetChunkMeshRevision(root))
               .has_value(),
           "resource-rejected async candidate recaptures current geometry");
    const auto retry_schedule =
        cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
    if (retry_schedule.Scheduled != 1)
      std::cerr << "resource retry scheduled=" << retry_schedule.Scheduled
                << " dirty=" << cache.GetDirtyCount()
                << " pending_capture=" << cache.HasPendingCaptureWork(root)
                << " snapshot_skips="
                << cache.GetLastMeshDirtyScheduleSkipSnapshotN()
                << " other_skips=" << cache.GetLastMeshDirtyScheduleSkipOtherN()
                << '\n';
    Expect(retry_schedule.Scheduled == 1 &&
               cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
           "actual async resource-rejection job finishes");
    cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
    Expect(cache.GetPublishCount(root) == published_count &&
               cache.GetPublishedRenderLightBinding(root) == binding &&
               cache.HasPendingDirty(),
           "async resource failure retains predecessor and retry obligation");
    reject_resource = false;

    const auto sync_binding = sync_cache.GetPublishedRenderLightBinding(root);
    const auto sync_count = sync_cache.GetPublishCount(root);
    Expect(bool(sync_binding), "synchronous candidate owns prepared resource");
    sync_cache.SetPrepareRenderLightFn(
        [](uint64_t, const SectionGeometryRenderInputs &,
           const SectionRenderLightSnapshot &)
            -> std::shared_ptr<SectionRenderLightBinding> { return {}; });
    sync_cache.RebuildChunkImmediate(world, registry, root);
    Expect(sync_cache.GetPublishCount(root) == sync_count &&
               sync_cache.GetPublishedRenderLightBinding(root) ==
                   sync_binding &&
               sync_cache.IsChunkMeshDirty(root),
           "immediate resource failure retains predecessor and retry ticket");
    sync_cache.RebuildDirtyChunksWithStats(world, registry, 100, 0, false, 0);
    Expect(sync_cache.GetPublishCount(root) == sync_count &&
               sync_cache.IsChunkMeshDirty(root),
           "bounded synchronous drain cannot erase the rejected retry ticket");
    for (const bool dead : {false, true})
    {
      sync_cache.SetPrepareRenderLightFn(
          [dead](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
                 const SectionRenderLightSnapshot &light)
              -> std::shared_ptr<SectionRenderLightBinding>
          {
            auto invalid = std::make_shared<TestRenderLightBinding>(
                dead ? epoch : epoch + 1, geometry, light);
            invalid->live = !dead;
            return invalid;
          });
      sync_cache.RebuildChunkImmediate(world, registry, root);
      Expect(
          sync_cache.GetPublishCount(root) == sync_count &&
              sync_cache.GetPublishedRenderLightBinding(root) == sync_binding &&
              sync_cache.IsChunkMeshDirty(root),
          "wrong-epoch or dead prepared resource cannot replace predecessor");
    }
    sync_cache.SetPrepareRenderLightFn(
        [&](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
            const SectionRenderLightSnapshot &light)
            -> std::shared_ptr<SectionRenderLightBinding>
        {
          if (use_gpu_bindings)
            return sync_gpu_bindings.Prepare(epoch, geometry, light);
          return std::make_shared<TestRenderLightBinding>(epoch, geometry,
                                                          light);
        });
    sync_cache.RebuildDirtyChunksWithStats(world, registry, 100, 0, false, 0);
    Expect(
        sync_cache.GetPublishCount(root) > sync_count &&
            !sync_cache.IsChunkMeshDirty(root),
        "resource recovery publishes a synchronous successor and drains retry");
    if (use_gpu_bindings)
    {
      const auto words = binding->ShaderWords();
#if defined(SECTION_LIGHT_BINDING_DRIVER)
      ProbeCertifiedBinding(gpu_bindings, binding);
#endif
      Expect(gpu_bindings.Bind(7, 8) &&
                 gpu_bindings.Refresh(binding, epoch, *physical_light) &&
                 binding->ShaderWords() == words,
             "physical field refresh preserves actual GPU binding identity");
      const auto current_binding =
          sync_cache.GetPublishedRenderLightBinding(root);
      const auto current_words = current_binding->ShaderWords();
      const auto current_geometry = current_binding->GeometryInputs();
      const auto before_field_only_count = sync_cache.GetPublishCount(root);
      const auto before_field_only_vertices = sync_cache.GetGreedyVertexCount();
      const auto sync_epoch = sync_cache.GetCaptureStore().WorldEpoch();
      for (const bool emitter_present : {true, false})
      {
        // Diagonal input changes physical transport but none of the seven
        // geometry face stamps used by this already published artifact.
        world.SetBlock({16, 8, 16}, emitter_present ? kFire : BLOCK_AIR);
        const auto changed =
            UChunkRelightSnapshot::CaptureSections(world, spec, {root})
                .Compute(registry);
        Expect(changed.inputs_complete && changed.chunks.size() == 1,
               "diagonal emitter has complete independent physical inputs");
        chunk->ReplaceComputedLightVolume(*changed.chunks[0].padded_light, true,
                                          true);
        Expect(world.RecordLightCalculation(changed, catalog) == 1,
               "diagonal emitter installs current physical calculation proof");
        const auto new_light = world.CaptureRenderLightSnapshot(root);
        Expect(new_light && new_light->MatchesGeometry(current_geometry) &&
                   UnpackBlock(
                       new_light->Volume()
                           ->cells[SectionLightVolume::Index(15, 8, 15)]) ==
                       (emitter_present ? 12 : 0),
               "diagonal emitter changes physical block light without geometry "
               "change");
        Expect(
            sync_gpu_bindings.Refresh(current_binding, sync_epoch,
                                      *new_light) &&
                current_binding->ShaderWords() == current_words &&
                sync_cache.GetPublishCount(root) == before_field_only_count &&
                sync_cache.GetGreedyVertexCount() == before_field_only_vertices,
            "certified light-only GPU replacement preserves binding and CPU "
            "geometry artifact");
#if defined(SECTION_LIGHT_BINDING_DRIVER)
        ProbeCertifiedBinding(sync_gpu_bindings, current_binding);
#endif
      }
      {
        SectionRenderLightBindings retirement_store;
        Expect(retirement_store.BeginWorld(
                   sync_epoch, current_geometry.world_geometry_identity, 1,
                   SectionLightVolume::CellCount, 1),
               "one-slot physical resource retirement fixture initializes");
        auto final_owner = retirement_store.Prepare(
            sync_epoch, current_geometry, current_binding->LightSnapshot());
        Expect(bool(final_owner), "one-slot physical resource acquired");
        const std::weak_ptr<SectionRenderLightBinding> destroyed = final_owner;
        glBindBuffer(GL_TEXTURE_2D, 0); // Deliberate caller GL_INVALID_ENUM.
        final_owner.reset();
        Expect(destroyed.expired() &&
                   !retirement_store.Prepare(sync_epoch, current_geometry,
                                             current_binding->LightSnapshot()),
               "failed final invalidation remains owned until explicit "
               "collection");
        retirement_store.CollectRetired();
        glFinish();
        retirement_store.CollectRetired();
        const auto reused = retirement_store.Prepare(
            sync_epoch, current_geometry, current_binding->LightSnapshot());
        Expect(reused && reused->IsLive(),
               "final lease release retries after GL error without leaking "
               "bounded capacity");
      }
      auto wrong_geometry = *published_geometry;
      ++wrong_geometry.stamps[0].content;
      Expect(
          !gpu_bindings.Prepare(epoch + 1, *published_geometry,
                                *physical_light) &&
              !gpu_bindings.Prepare(epoch, wrong_geometry, *physical_light) &&
              !gpu_bindings.Refresh(binding, epoch + 1, *physical_light),
          "wrong logical world epoch and geometry cannot acquire physical GPU "
          "authority");
      Expect(gpu_bindings.BeginWorld(
                 epoch, published_geometry->world_geometry_identity, 16,
                 8u * SectionLightVolume::CellCount, 8) &&
                 !binding->IsLive() &&
                 binding->ShaderWords() == std::array<uint32_t, 4>{} &&
                 !gpu_bindings.Refresh(binding, epoch, *physical_light),
             "world reset invalidates retained leases even with reused logical "
             "epoch");
      const auto replacement =
          gpu_bindings.Prepare(epoch, *published_geometry, *physical_light);
      Expect(replacement && replacement->IsLive() &&
                 replacement->ShaderWords() != words,
             "new world binding cannot alias a retained predecessor handle");
#if defined(SECTION_LIGHT_BINDING_DRIVER)
      ProbeCertifiedBinding(gpu_bindings, replacement);
#endif
    }
    cache.MarkDirty(root);
    cache.GetCaptureStore().Invalidate(root);
    cache.SetIsLightRepairRemeshFn([](glm::ivec3) { return false; });
    ChunkMeshCaptureAdmissionTestAccess::ReserveOneRepair(cache);
    Expect(!ChunkMeshCaptureAdmissionTestAccess::Acquire(cache, world, registry,
                                                         root),
           "ordinary bright remesh cannot consume the repair reservation");
    Expect(cache.PrioritizeVisibleLightRepairRemesh(root),
           "required geometry debt owns explicit priority repair");
    Expect(ChunkMeshCaptureAdmissionTestAccess::Acquire(cache, world, registry,
                                                        root),
           "priority bright geometry repair can spend one reserved capture "
           "credit");
    cache.GetCaptureStore().Invalidate(root);
    Expect(
        !ChunkMeshCaptureAdmissionTestAccess::Acquire(cache, world, registry,
                                                      root),
        "priority repair cannot spend the same bounded capture credit twice");
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

int main(int argc, char **argv)
{
  const std::shared_ptr<cutum::UBlockDefinitionStorage> definitions =
      MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);

  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());

#if defined(SECTION_LIGHT_BINDING_DRIVER)
  if (argc == 2 && std::string(argv[1]) == "--light-binding-driver")
  {
    if (!glfwInit())
      return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto *window = glfwCreateWindow(32, 32, "Certified render light test",
                                    nullptr, nullptr);
    if (!window)
    {
      glfwTerminate();
      return 77;
    }
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
      glfwDestroyWindow(window);
      glfwTerminate();
      return 77;
    }
    while (glGetError() != GL_NO_ERROR)
    {
    }
    std::cout << "GPU: " << glGetString(GL_RENDERER)
              << " GL: " << glGetString(GL_VERSION) << '\n';
    TestFirstComputedLightPublication(registry, definitions.get(), true);
    Expect(glGetError() == GL_NO_ERROR,
           "certified binding integration has no GL errors");
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "section_render_light_bindings_driver_test: PASS "
                 "(physical proof/publication/resource lifetime; no world "
                 "shader acceptance)\n";
    return 0;
  }
#endif
  TestCrossGrassVisibleWithFrustum(world, registry);
  TestCampfireStoneMesh(world, registry);
  TestGreedyAlphaCutoutIsMaterialScoped(world, registry);
  TestOverflowRetryWithNoDirtyAdmission(registry);
  TestNoSurfaceGeometryProof(registry, definitions.get());
  TestSecondPlaneGeometryParity(registry, definitions.get());
  TestOccludedMeshDependencyRepair(registry);
  TestPublicationRefreshesDrawRefs(registry);
  TestFirstComputedLightPublication(registry, definitions.get());
  TestWorldDemandIsolation();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
