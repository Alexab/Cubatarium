#include "Render/Mesh/GpuFluidColumnScan.h"
#include "World/Mesh/WorldMeshService.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Jobs/PipelineAdmission.h"
#include "Render/Camera/Frustum.h"
#include "Render/Lighting/SectionRenderLightBindings.h"
#if defined(SECTION_LIGHT_BINDING_DRIVER)
#include "Render/Engine/CrossGpuBackend.h"
#include "Render/Engine/MdiVertexPoolStore.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Lighting/SectionLightBindingTable.h"
#include "Render/Lighting/SectionLightDrawTable.h"
#include <GLFW/glfw3.h>
#endif
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Mesh/FaceMaskComputeSource.h"
#include "Render/Mesh/GpuGreedyFaceExtract.h"
#include "Render/Mesh/GpuGreedyMesher.h"
#include "Render/Mesh/GpuGreedyOpaqueEmit.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/GreedyMesher.h"
#include "Render/Mesh/IUChunkMesher.h"
#include "Render/Mesh/MeshCaptureToken.h"
#include "Render/Pipeline/GreedyDrawCategory.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/FlatLightingPipeline.h"
#include "World/Streaming/DependencyStampBuilder.h"
#include "WorldGen/Core/CatalogPublicationRoots.h"
#include <nlohmann/json.hpp>
#include <thread>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

namespace cutum
{
#if defined(SECTION_LIGHT_BINDING_DRIVER)
struct GpuMeshPipelineReadbackTestAccess
{
  static UGpuMeshPipeline::GpuApplyTicket Acquire(UGpuMeshPipeline &pipeline)
  {
    UGpuMeshPipeline::GpuApplyTicket ticket;
    pipeline.AcquireReadbackSlot(ticket);
    return ticket;
  }
  static void ResetStorage(UGpuMeshPipeline &pipeline)
  {
    pipeline.DestroyReadbackPbo();
  }
};
struct MdiVertexPoolStoreDriverTestAccess
{
  static void Rebuild(UMdiVertexPoolStore &store, GreedyGpuPassCache &cache)
  {
    store.RebuildIndirectCmdTable(cache);
  }
};
struct SectionLightDrawTableTestAccess
{
  static void FailTransfer(SectionLightDrawTable &table, bool enabled)
  {
    table.BeforeTransfer =
        enabled ? +[](void *) { glBindBuffer(GL_ARRAY_BUFFER, 0); } : nullptr;
  }
};
struct CrossGpuBackendDriverTestAccess
{
  struct Fault
  {
    unsigned fail_on{0}, calls{0};
  };
  static void Arm(UCrossGpuBackend &backend, Fault &fault)
  {
    backend.TransferContext = &fault;
    backend.BeforeTransfer = [](unsigned target, void *context)
    {
      auto &f = *static_cast<Fault *>(context);
      if (++f.calls == f.fail_on)
        glBindBuffer(target, 0); // The actual following transfer must fail.
    };
  }
  static void *Fence(const UCrossGpuBackend &backend)
  {
    return backend.StagingFence;
  }
  static void Clear(UCrossGpuBackend &backend)
  {
    backend.BeforeTransfer = nullptr;
    backend.TransferContext = nullptr;
  }
};
#endif
struct MeshBuilderAdmissionTestAccess
{
  static void LimitPool(UAsyncMeshBuilder &builder) { builder.Pool.SetMaxPendingJobCount(1); }
  // Controlled stale-delivery fault: the real completed result retains its
  // retry permission while the submission epoch changes before delivery.
  static void AdvanceDeliveryEpoch(UAsyncMeshBuilder &builder)
  { builder.Epoch.fetch_add(1,std::memory_order_acq_rel); }
};

struct ChunkMeshCaptureAdmissionTestAccess
{
  static void ParkExistingRemesh(UChunkMeshCache &cache, glm::ivec3 coord)
  {
    if (cache.DeferRemeshCoord(coord))
      cache.Dirty.Erase(coord);
  }

  static std::optional<ChunkMeshSnapshot>
  CaptureForSchedule(UChunkMeshCache &cache, const UBlockWorld &world,
                     glm::ivec3 coord, uint64_t revision)
  {
    cache.ConfigureCapturePolicy(world);
    return cache.GetCaptureStore().CaptureAndStore(world, coord, revision);
  }
  static bool AdmitCpuThenAdvanceReceiptFrame(UChunkMeshCache &cache,
      ChunkMeshSnapshot snapshot, UBlockRegistry &registry)
  {
    cache.EnsureAsyncBuilder();
    const auto coord = snapshot.coord;
    const auto revision = snapshot.sourceRevision;
    if (!cache.AsyncBuilder->Enqueue(std::move(snapshot), registry)) return false;
    cache.ActiveMeshSourceRevision[coord] = revision;
    cache.Dirty.Erase(coord);
    // Isolate the actual admission/owner handoff and next-frame marker reset;
    // receipt consumption itself uses the public cache API under test.
    cache.ScheduledThisFrame_.clear();
    return true;
  }
  static void TrackSource(UChunkMeshCache &cache, glm::ivec3 coord,
                          uint64_t revision)
  {
    cache.ActiveMeshSourceRevision[coord] = revision;
  }
  static bool TracksSource(const UChunkMeshCache &cache, glm::ivec3 coord,
                           uint64_t revision)
  {
    const auto it = cache.ActiveMeshSourceRevision.find(coord);
    return it != cache.ActiveMeshSourceRevision.end() && it->second == revision;
  }
  static void Apply(UChunkMeshCache &cache, const UBlockWorld &world,
                    UBlockRegistry &registry, MeshBuildResult &&result)
  {
    cache.ApplyMeshResult(world, registry, std::move(result));
  }
  static glm::ivec3 AwaitingLightBack(const UChunkMeshCache &cache)
  {
    return cache.GeometryAwaitingLightQueue_.back();
  }
  static glm::ivec3 AwaitingLightFront(const UChunkMeshCache &cache)
  {
    return cache.GeometryAwaitingLightQueue_.front();
  }
  static std::optional<uint64_t>
  PublishedOriginalLight(const UChunkMeshCache &cache, glm::ivec3 coord)
  {
    const auto found = cache.GreedyCache.find(coord);
    if (found == cache.GreedyCache.end() ||
        !found->second.PublishedGeomStampsValid)
      return std::nullopt;
    return found->second.PublishedGeomStamps[0].light;
  }
  static ChunkMeshSnapshot CaptureOwned(UChunkMeshCache &cache,
                                        const UBlockWorld &world,
                                        glm::ivec3 coord)
  {
    cache.ConfigureCapturePolicy(world);
    const auto epoch = cache.GetCaptureStore().WorldEpoch();
    auto snapshot = ChunkMeshSnapshot::Capture(
        world, coord, cache.GetChunkMeshRevision(coord), nullptr, nullptr,
        cache.GetCaptureStore().LightInputsPolicy());
    snapshot.sourceWorldEpoch = epoch;
    snapshot.sourceCaptureGeneration =
        cache.GetCaptureStore().CapturePolicyGeneration();
    return snapshot;
  }
  static bool PrepareOnly(UChunkMeshCache &cache, const UBlockWorld &world,
                          const ChunkMeshSnapshot &snapshot,
                          UBlockRegistry &registry,
                          const SectionNoSurfaceProof *no_surface = nullptr)
  {
    std::shared_ptr<SectionRenderLightBinding> prepared;
    return cache.TryPrepareRenderLight(
        snapshot.sourceWorldEpoch,
        snapshot.inputStampsValid ? &snapshot.inputStamps : nullptr,
        snapshot.GeometryRenderInputs(registry.GetDefinitionsCatalogSnapshot()),
        snapshot.lightInputs == MeshLightInputs::PhysicalField
            ? world.CaptureRenderLightSnapshot(snapshot.coord)
            : snapshot.renderLight,
        snapshot.provisionalLightPreview, prepared, no_surface);
  }
  static bool CommitEmptyPacked(UChunkMeshCache &cache,
                                const UBlockWorld &world,
                                UBlockRegistry &registry,
                                const ChunkMeshSnapshot &snapshot)
  {
    GpuMeshProcessResult result;
    result.success = true;
    return cache.CommitGpuMeshResult(
        world, registry, snapshot.coord,
        cache.GetChunkMeshRevision(snapshot.coord), std::move(result), {},
        false, snapshot.inputStamps[0].light, true, {}, false, {},
        snapshot.inputStampsValid ? &snapshot.inputStamps : nullptr,
        snapshot.GeometryRenderInputs(registry.GetDefinitionsCatalogSnapshot()),
        snapshot.renderLight, snapshot.sourceWorldEpoch, nullptr,
        &snapshot.lightHaloSignatures, snapshot.lightInputs,
        snapshot.sourceCaptureGeneration);
  }
#if defined(SECTION_LIGHT_BINDING_DRIVER)
  static void QueuePacked(UChunkMeshCache &cache, ChunkMeshSnapshot snapshot,
                          UBlockRegistry &registry)
  {
    cache.EnsureAsyncBuilder();
    if (!cache.GpuPipeline)
    {
      cache.GpuPipeline = std::make_unique<UGpuMeshPipeline>();
      if (!cache.GpuPipeline->Init(2))
        std::abort();
    }
    cache.GpuPipelineInitAttempted = true;
    UChunkMeshCache::PendingGpuApply pending;
    pending.coord = snapshot.coord;
    pending.sourceRevision = snapshot.sourceRevision;
    pending.snapshot = std::move(snapshot);
    pending.inputCatalog = registry.GetDefinitionsCatalogSnapshot();
    pending.queuedAt = pending.phaseSince = std::chrono::steady_clock::now();
    cache.ActiveMeshSourceRevision[pending.coord] = pending.sourceRevision;
    cache.GpuExtractInFlight.insert(pending.coord);
    cache.PendingGpuApplies.push_back(std::move(pending));
    cache.TouchPendingGpuIndex();
  }
  static void DrainPacked(UChunkMeshCache &cache, UBlockWorld &world,
                          UBlockRegistry &registry)
  {
    MeshRebuildTickStats stats;
    cache.ProcessPendingGpuMeshes(world, registry, 8, 1000, stats);
  }
  static void InitPacked(UChunkMeshCache &cache)
  {
    cache.EnsureAsyncBuilder();
    cache.GpuPipeline = std::make_unique<UGpuMeshPipeline>();
    if (!cache.GpuPipeline->Init(2)) std::abort();
    cache.GpuPipelineInitAttempted = true;
  }
  static std::vector<int> HoldAllPackedSlots(UChunkMeshCache &cache)
  {
    std::vector<int> slots;
    auto &allocator = cache.GpuPipeline->GetAllocator();
    while (allocator.GetFreeSlotCount())
      slots.push_back(allocator.AllocateStagingSlot(false));
    return slots;
  }
  static void ReleaseHeldPackedSlots(UChunkMeshCache &cache,
                                      const std::vector<int> &slots)
  {
    for (const auto slot : slots)
      cache.GpuPipeline->GetAllocator().FreeSlotByIndex(slot);
  }
  static uint32_t FreePackedSlots(UChunkMeshCache &cache)
  {
    return cache.GpuPipeline->GetAllocator().GetFreeSlotCount();
  }
  static UGpuMeshPipeline::GpuApplyTicket &KickOnly(UChunkMeshCache &cache,
                                                    UBlockRegistry &registry)
  {
    auto &pending = cache.PendingGpuApplies.front();
    const int slot =
        cache.GpuPipeline->GetAllocator().AllocateStagingSlot(false);
    if (slot < 0 || !cache.GpuPipeline->KickComputePasses(
                        pending.snapshot, registry, pending.coord, slot,
                        pending.ticket, pending.inputCatalog.get()))
      std::abort();
    pending.phase = pending.ticket.awaitingCounters
                        ? UChunkMeshCache::PendingGpuApply::Phase::Dispatched
                        : UChunkMeshCache::PendingGpuApply::Phase::Kicked;
    cache.TouchPendingGpuIndex();
    return pending.ticket;
  }
  static bool AdvancePackedToKicked(UChunkMeshCache &cache, UBlockRegistry &registry)
  {
    auto &pending = cache.PendingGpuApplies.front();
    glFinish();
    const auto status = cache.GpuPipeline->TryCompleteCountersAndEmit(
        pending.ticket, registry, 0);
    if (status != UGpuMeshPipeline::GpuFinishStatus::Ready ||
        !pending.ticket.valid || pending.ticket.awaitingCounters || !pending.ticket.fence)
      return false;
    pending.phase = UChunkMeshCache::PendingGpuApply::Phase::Kicked;
    cache.TouchPendingGpuIndex();
    return true;
  }
  static bool HasCpuRecovery(UChunkMeshCache &cache, glm::ivec3 coord)
  {
    return cache.CpuMeshRecovery.count(coord) != 0;
  }
#endif
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
        snapshot.renderLight, snapshot.sourceWorldEpoch, nullptr, nullptr,
        snapshot.lightInputs, snapshot.sourceCaptureGeneration);
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
constexpr cutum::BlockId kSolidCutout = 635;
constexpr cutum::BlockId kTransparentCutout = 636;
constexpr cutum::BlockId kSolidCross = 637;
constexpr cutum::BlockId kWater = 638;
constexpr cutum::BlockId kCubeLiquid = 639;

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
  bool SupportsLightRefresh() const override { return refreshable; }
  bool RefreshLight(uint64_t epoch,
                    const cutum::SectionRenderLightSnapshot &light) override
  {
    if (!refreshable || reject_refresh || !live || epoch != WorldEpoch() ||
        !light.MatchesGeometry(GeometryInputs()))
      return false;
    SetCompatibleLight(light);
    if (on_refresh)
      on_refresh();
    return live;
  }
  bool live{true};
  bool refreshable{false};
  bool reject_refresh{false};
  std::function<void()> on_refresh;
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
static void ProbeWorkingProgram(cutum::UShaderProgram &program,
                                const std::string &uniform)
{
  GLuint vao = 0, texture = 0, fbo = 0;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenTextures(1, &texture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  Expect(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
         "shader replacement probe framebuffer is complete");
  glViewport(0, 0, 1, 1);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  program.Use();
  program.SetFloat(uniform, .75f);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  std::array<uint8_t, 4> pixel{};
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
  Expect(pixel[0] >= 190 && pixel[0] <= 193 && pixel[1] == 0 && pixel[2] == 0 &&
             pixel[3] == 255,
         "working shader predecessor still draws its expected uniform color");
  program.Unuse();
  glBindVertexArray(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &texture);
  glDeleteVertexArrays(1, &vao);
  Expect(glGetError() == GL_NO_ERROR,
         "shader replacement probe has no GL errors");
}

static void TestShaderProgramFailureAtomicity(bool gles)
{
  using namespace cutum;
  const std::string version =
      gles ? "#version 300 es\nprecision highp float;precision highp int;\n"
           : "#version 330 core\n";
  const auto vertex =
      version +
      "out vec3 linkedValue;void main(){"
      "linkedValue=vec3(1,0,0);vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
      "gl_Position=vec4(p*2.-1.,0,1);}";
  const auto fragment =
      version + "in vec3 linkedValue;uniform float uValue;"
                "out vec4 color;void main(){color=vec4(linkedValue*uValue,1);}";
  const auto bad_vertex = version + "void main(){ invalid_shader_tokens; }";
  const auto bad_fragment = version + "void main(){ invalid_fragment_tokens; }";
  const auto bad_link = version + "in vec4 linkedValue;out vec4 color;"
                                  "void main(){color=linkedValue;}";
  UShaderProgram program;
  Expect(!program.CreateFromStrings(bad_vertex, fragment) &&
             !program.IsValid() && program.GetProgramID() == 0,
         "failed first shader compilation exposes no program");
  Expect(!program.CreateFromStrings(vertex, bad_link) && !program.IsValid(),
         "failed first shader link exposes no program");
  Expect(program.CreateFromStrings(vertex, fragment) && program.IsValid(),
         "shader program recovers after failed initial candidates");
  const auto previous = program.GetProgramID();
  const auto location = program.GetUniformLocation("uValue");
  Expect(location >= 0, "working program caches its active uniform");
  ProbeWorkingProgram(program, "uValue");
  const auto preserved = [&]()
  {
    GLint linked = 0;
    glGetProgramiv(previous, GL_LINK_STATUS, &linked);
    Expect(program.GetProgramID() == previous && program.IsValid() && linked &&
               program.GetUniformLocation("uValue") == location,
           "failed shader replacement retains linked predecessor and uniform "
           "cache");
    ProbeWorkingProgram(program, "uValue");
  };
  Expect(!program.CreateFromStrings(bad_vertex, fragment),
         "malformed vertex candidate is rejected");
  preserved();
  Expect(!program.CreateFromStrings(vertex, bad_fragment),
         "malformed fragment candidate is rejected");
  preserved();
  Expect(!program.CreateFromStrings(vertex, bad_link),
         "unlinked shader candidate is rejected");
  preserved();
  Expect(!program.CreateFromFiles("__missing_shader_vertex__",
                                  "__missing_shader_fragment__"),
         "missing shader sources cannot replace a working program");
  preserved();
  std::string replacement = fragment;
  replacement.replace(replacement.find("uValue"), 6, "uReplacement");
  replacement.replace(replacement.find("uValue"), 6, "uReplacement");
  Expect(program.CreateFromStrings(vertex, replacement) &&
             program.GetProgramID() != previous && !glIsProgram(previous) &&
             program.GetUniformLocation("uValue") == -1 &&
             program.GetUniformLocation("uReplacement") >= 0,
         "successful shader replacement retires predecessor and clears uniform "
         "cache");
  ProbeWorkingProgram(program, "uReplacement");
  UShaderManager manager;
  const auto cached = manager.CreateShaderFromStrings("kept", vertex, fragment);
  Expect(cached && !manager.CreateShaderFromStrings("kept", vertex, bad_link) &&
             manager.GetShader("kept") == cached && cached->IsValid(),
         "manager keeps prior named program after rejected replacement");
  ProbeWorkingProgram(*cached, "uValue");
  Expect(glGetError() == GL_NO_ERROR,
         "shader failure cases leave no GL errors");
}

static void TestProductionGreedyPrograms(bool gles, bool packed)
{
  const std::string root = std::string(CUBATARIUM_SOURCE_DIR) + "/shaders/";
  const std::string directory = root + (gles ? "gles/" : "");
  for (const std::string file :
       {"vshader_greedy.glsl", "vshader_cross_instanced.glsl"})
  {
    cutum::UShaderProgram program;
    Expect(program.CreateFromFiles(directory + file,
                                   directory + "fshader_greedy.glsl"),
           "production greedy/Cross vertex and fragment shader pair links");
    GLint linked = 0;
    glGetProgramiv(program.GetProgramID(), GL_LINK_STATUS, &linked);
    Expect(linked == GL_TRUE,
           "production program reports actual GL link success");
    float alpha = 0;
    glGetUniformfv(program.GetProgramID(),
                   program.GetUniformLocation("uFluidAlpha"), &alpha);
    Expect(alpha == 1.f,
           "production greedy/Cross default alpha is explicitly opaque");
    const auto previous = program.GetProgramID();
    program.Use();
    Expect(
        program.CreateFromFiles(directory + file,
                                directory + "fshader_greedy.glsl"),
        "file shader defaults initialize before replacing an active program");
    GLint active = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &active);
    Expect(static_cast<GLuint>(active) == previous,
           "candidate file defaults restore prior active GL program");
    program.Use();
    Expect(!glIsProgram(previous),
           "using file successor releases deleted active predecessor");
    program.Unuse();
  }
  if (packed)
  {
    cutum::UShaderProgram program;
    Expect(program.CreateFromFiles(root + "vshader_packed_greedy.glsl",
                                   root + "fshader_greedy.glsl"),
           "production desktop packed shader pair links");
  }
  for (const bool skinned : {false, true})
  {
    cutum::UShaderProgram program;
    Expect(program.CreateFromFiles(
               directory +
                   (skinned ? "vshader_creature_skinned.glsl" : "vshader.glsl"),
               directory + (skinned ? "fshader_creature_skinned.glsl"
                                    : "fshader_creature.glsl")),
           "production rigid/skinned creature shader pair links");
    std::array<float, 4> tint{};
    glGetUniformfv(program.GetProgramID(), program.GetUniformLocation("uTint"),
                   tint.data());
    Expect(tint == std::array<float, 4>({1, 1, 1, 1}),
           "production creature default tint is white on desktop and GLES");
    const std::pair<const char *, float> defaults[] = {
        {"uCreatureLitEnabled", 0}, {"uEnvMinAmbient", .12f},
        {"uEnvDayFactor", 1},       {"uEnvNightFactor", 0},
        {"uEnvSkyLightScale", 1},   {"uCreatureFogFactor", 0},
        {"uFogEnabled", 0}};
    for (const auto &entry : defaults)
    {
      float value = -1;
      const auto location = program.GetUniformLocation(entry.first);
      Expect(location >= 0, "creature default uniform remains active");
      glGetUniformfv(program.GetProgramID(), location, &value);
      Expect(
          std::abs(value - entry.second) < .001f,
          "production creature file defaults preserve lighting and fog policy");
    }
    std::array<float, 2> light{};
    glGetUniformfv(program.GetProgramID(),
                   program.GetUniformLocation("uCreatureLight"), light.data());
    Expect(light == std::array<float, 2>({1, 0}),
           "creature default light is direct sky only");
  }
  Expect(glGetError() == GL_NO_ERROR,
         "production shader loading has no GL errors");
}
#endif

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
  const auto prepared_draw = store.PrepareDraw(binding);
  Expect(prepared_draw.has_value(),
         "physical source prepares a resident or ordered draw");
  const auto words = *prepared_draw;
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

  auto solid_cutout = stone;
  solid_cutout.Name = "solid_cutout";
  solid_cutout.Render.Style = cutum::BlockRenderStyle::Cutout;
  by_id[kSolidCutout] = solid_cutout;
  auto transparent_cutout = solid_cutout;
  transparent_cutout.Name = "transparent_cutout";
  transparent_cutout.Render.Transparent = true;
  by_id[kTransparentCutout] = transparent_cutout;
  auto solid_cross = stone;
  solid_cross.Name = "solid_cross";
  solid_cross.Render.Style = cutum::BlockRenderStyle::Cross;
  by_id[kSolidCross] = solid_cross;
  auto water = stone;
  water.Name = "water";
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  water.Render.Style = cutum::BlockRenderStyle::Fluid;
  water.Render.Transparent = true;
  by_id[kWater] = water;
  auto cube_liquid = water;
  cube_liquid.Name = "cube_liquid";
  cube_liquid.Render.Style = cutum::BlockRenderStyle::UCube;
  cube_liquid.Physics.Movement.Occupancy = 1.0f;
  by_id[kCubeLiquid] = cube_liquid;
  for (auto id :
       {kSolidCutout, kTransparentCutout, kSolidCross, kWater, kCubeLiquid})
    name_to_id[by_id[id].Name] = id;
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

static const cutum::GreedyMeshBatch *
FindGreedyBatchForBlock(const cutum::UChunkMeshCache &cache,
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

static void
TestGreedyAlphaCutoutIsMaterialScoped(cutum::UBlockWorld &world,
                                      cutum::UBlockRegistry &registry)
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
  RebuildAndCull(cache, world, registry, glm::vec3(32.5f, 13.5f, 36.0f));

  const auto &opaque_refs = cache.GetGreedyOpaqueCutoutRefs();
  const auto *stone_batch = FindGreedyBatchForBlock(cache, opaque_refs, kStone);
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

static void TestDenseAirMeshCapture(cutum::UBlockRegistry &registry)
{
  using namespace cutum;
  for (const auto root : {glm::ivec3(1, -4, 3), glm::ivec3(1, 1, 3)})
    for (int axis = 0; axis < 3; ++axis)
      for (int sign : {-1, 1})
      {
        glm::ivec3 offset(0);
        offset[axis] = sign;
        const auto air_coord = root + offset;
        if (air_coord.y < -4)
          continue;
        UBlockWorld world;
        auto profile = world.GetVerticalProfile();
        profile.MinY = -64;
        profile.MaxY = 319;
        world.SetVerticalProfile(profile);
        world.SetBlock(root * CHUNK_SIZE + glm::ivec3(8, 15, 8), kStone);
        UChunk legacy(air_coord);
        std::array<uint8_t, CHUNK_VOLUME> light;
        light.fill(0x53);
        legacy.ReplaceLightData(light);
        legacy.SetWeatherState(.5f, 123.125);
        UBinaryChunkSerializer serializer;
        const auto bytes = serializer.Serialize(air_coord, legacy, registry);
        const auto decoded =
            serializer.Deserialize(bytes.bytes, air_coord, registry);
        const auto air = decoded.GetDecodedAirSlice(air_coord);
        auto &manager = world.GetChunkManager();
        const ChunkGenerationToken token{{air_coord.x, 0, air_coord.z}, 0};
        Expect(air && manager.InstallLoadedAirSlice(*air, token, 0, false) &&
                   !manager.HasChunk(air_coord) &&
                   world.IsKnownAbsentAirChunk(air_coord),
               "dense decoded AIR fixture has explicit empty geometry without "
               "a holder");
        Expect(!ChunkMeshSnapshot::IsAbsentOpenSkyChunk(world, air_coord),
               "loaded internal AIR does not imply open sky illumination");
        const auto snapshot = ChunkMeshSnapshot::Capture(
            world, root, 7, nullptr, nullptr, MeshLightInputs::PhysicalField);
        Expect(snapshot.inputStampsValid && snapshot.InputsStillValid(world),
               "new geometry capture with dense loaded AIR must be immediately "
               "current");
        const unsigned face = axis * 2 + (sign > 0 ? 1 : 0);
        Expect((snapshot.boundaryOverlay.missingNeighborFaces & (1u << face)) ==
                   0,
               "explicit loaded AIR is not unknown shell debt");
        UMeshCaptureStore store;
        store.SetLightInputsPolicy(MeshLightInputs::PhysicalField);
        Expect(store.CaptureAndStore(world, root, 7) &&
                   store.TryGet(world, root, 7) && store.TryGet(world, root, 7),
               "dense AIR geometry capture survives real commit and repeated "
               "cache lookup");
        const auto baked = ChunkMeshSnapshot::Capture(world, root, 7);
        const unsigned light_offset =
            (offset.y + 1) * 9 + (offset.z + 1) * 3 + offset.x + 1;
        Expect((baked.openSkyAbsentMask & (1u << light_offset)) == 0,
               "geometry AIR proof must not enable legacy sky15 fallback");
        manager.RemoveChunk(air_coord);
        Expect(!snapshot.InputsStillValid(world) &&
                   !store.TryGet(world, root, 7),
               "AIR retirement invalidates captured known-empty dependencies");
        manager.EnsureSectionLightField(air_coord);
        Expect(!ChunkMeshSnapshot::IsKnownAirAbsentFaceNeighbor(
                   world, air_coord, axis, sign),
               "field allocation alone cannot certify mesh AIR boundary");
        const auto unknown = ChunkMeshSnapshot::Capture(
            world, root, 7, nullptr, nullptr, MeshLightInputs::PhysicalField);
        Expect(unknown.InputsStillValid(world) &&
                   (unknown.boundaryOverlay.missingNeighborFaces &
                    (1u << face)) != 0,
               "unknown shell remains explicitly unknown and self-consistent");
      }
}

static void
TestPhysicalCaptureOwnership(cutum::UBlockRegistry &registry,
                             const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  const glm::ivec3 root(0);
  world.SetBlock({8, 8, 8}, kStone);
  UMeshCaptureStore store;
  const auto epoch = store.WorldEpoch();
  const auto original_generation = store.CapturePolicyGeneration();
  auto baked = store.CaptureAndStore(world, root, 7);
  Expect(baked.has_value(), "capture policy initial owned capture");
  Expect(store.SetLightInputsPolicy(MeshLightInputs::PhysicalField) &&
             store.Size() == 0 && store.WorldEpoch() == epoch &&
             store.CapturePolicyGeneration() != original_generation,
         "capture policy change clears captures without changing world epoch");
  const MeshCaptureToken token{epoch, 7, 1, store.LightInputsPolicy(),
                               store.CapturePolicyGeneration()};
  auto band = world.ReadChunkBandForCapture(root, token);
  Expect(band && band->lightInputs == MeshLightInputs::PhysicalField &&
             band->sourceWorldEpoch == epoch &&
             band->sourceCaptureGeneration == token.capture_policy_generation,
         "world band records actual capture policy and generation");
  auto deps = BuildMeshCaptureDependencyStamp(world, root, 7, registry,
                                              token.light_inputs,
                                              token.capture_policy_generation);
  Expect(store.TryCommit(root, 7, epoch, *band, &deps),
         "current physical band commits with matching dependency owner");
  const auto stable_generation = store.CapturePolicyGeneration();
  Expect(!store.SetLightInputsPolicy(MeshLightInputs::PhysicalField) &&
             store.CapturePolicyGeneration() == stable_generation &&
             store.Size() == 1,
         "repeated capture policy preserves cached capture and generation");
  auto *chunk = world.GetChunkManager().GetChunk(root);
  chunk->SetLightLocal({8, 9, 8}, 15, 4);
  auto current_deps = BuildMeshCaptureDependencyStamp(
      world, root, 7, registry, token.light_inputs,
      token.capture_policy_generation);
  Expect(CaptureDependencyStillValid(deps, current_deps) &&
             deps.Matches(current_deps) && store.TryGet(world, root, 7),
         "physical capture owner survives actual field mutation");
  auto wrong_deps = deps;
  ++wrong_deps.capture_policy_generation;
  Expect(!store.TryCommit(root, 7, epoch, *band, &wrong_deps) &&
             !CaptureDependencyStillValid(deps, wrong_deps) &&
             !deps.Matches(wrong_deps),
         "capture dependency generation rejects obsolete owner");
  wrong_deps = deps;
  wrong_deps.mesh_light_inputs = MeshLightInputs::Baked;
  Expect(!store.TryCommit(root, 7, epoch, *band, &wrong_deps) &&
             !CaptureDependencyStillValid(deps, wrong_deps),
         "capture dependency policy rejects incompatible inputs");
  const auto refreshed = store.RefreshIncrementalShell(world, root, 7, 0x3f);
  Expect(refreshed &&
             refreshed->lightInputs == MeshLightInputs::PhysicalField &&
             std::none_of(refreshed->inputStamps.begin(),
                          refreshed->inputStamps.end(),
                          [](const auto &stamp) { return stamp.readsLight; }),
         "physical shell refresh preserves geometry-only stamps");
  world.SetBlock({8, 8, 8}, BLOCK_AIR);
  Expect(!store.TryGet(world, root, 7),
         "physical capture owner still rejects actual geometry mutation");
  store.SetLightInputsPolicy(MeshLightInputs::Baked);
  Expect(!store.TryCommit(root, 7, epoch, *baked),
         "capture policy ABA rejects old same-mode owned result");
  store.SetLightInputsPolicy(MeshLightInputs::PhysicalField);
  Expect(!store.TryCommit(root, 7, epoch, *band, &deps),
         "physical capture policy ABA rejects old owned result");
  auto unowned = ChunkMeshSnapshot::Capture(world, root, 7, nullptr, nullptr,
                                            MeshLightInputs::PhysicalField);
  auto partial_owner = unowned;
  partial_owner.sourceWorldEpoch = epoch;
  Expect(!store.TryCommit(root, 7, epoch, partial_owner),
         "capture with missing policy generation cannot be rebound");
  Expect(store.TryCommit(root, 7, epoch, unowned),
         "explicit unowned raw capture can be adopted by current owner");
  auto invalid_token = token;
  invalid_token.capture_policy_generation = 0;
  Expect(!world.ReadChunkBandForCapture(root, invalid_token),
         "world band rejects missing capture policy generation");
}

static void TestMeshResultCaptureOwnership(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  for (const auto policy :
       {MeshLightInputs::Baked, MeshLightInputs::PhysicalField})
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    world.SetBlock({8, 8, 8}, kStone);
    const glm::ivec3 root(0);
    UChunkMeshCache cache;
    cache.MarkDirtyPriority(root);
    auto &store = cache.GetCaptureStore();
    store.SetLightInputsPolicy(policy);
    const auto revision = cache.GetChunkMeshRevision(root);
    const auto captured = store.CaptureAndStore(world, root, revision);
    Expect(bool(captured),
           "mesh policy fixture captures actual owned geometry");
    UAsyncMeshBuilder builder(1);
    Expect(builder.Enqueue(*captured, registry) &&
               builder.WaitIdleFor(std::chrono::seconds(5)),
           "actual mesh worker completes versioned capture");
    auto results = builder.DrainCompleted(1);
    Expect(results.size() == 1 && results[0].lightInputs == policy &&
               results[0].sourceCaptureGeneration ==
                   captured->sourceCaptureGeneration &&
               results[0].sourceWorldEpoch == captured->sourceWorldEpoch,
           "actual mesh worker preserves original capture policy generation");
    store.SetLightInputsPolicy(policy == MeshLightInputs::Baked
                                   ? MeshLightInputs::PhysicalField
                                   : MeshLightInputs::Baked);
    store.SetLightInputsPolicy(policy);
    const auto replacement = store.CaptureAndStore(world, root, revision);
    Expect(
        replacement && replacement->sourceCaptureGeneration !=
                           captured->sourceCaptureGeneration,
        "replacement capture has a new generation with same source revision");
    ChunkMeshCaptureAdmissionTestAccess::TrackSource(cache, root, revision);
    ChunkMeshCaptureAdmissionTestAccess::Apply(cache, world, registry,
                                               std::move(results[0]));
    Expect(cache.GetPublishCount(root) == 0 &&
               ChunkMeshCaptureAdmissionTestAccess::TracksSource(cache, root,
                                                                 revision) &&
               store.TryGet(world, root, revision),
           "obsolete mesh result cannot publish or erase replacement capture "
           "owner");
    if (policy == MeshLightInputs::PhysicalField)
    {
      Expect(builder.Enqueue(*replacement, registry) &&
                 builder.WaitIdleFor(std::chrono::seconds(5)),
             "unsupported receiver fixture builds actual current physical "
             "geometry");
      auto unsupported = builder.DrainCompleted(1);
      Expect(unsupported.size() == 1,
             "unsupported receiver gets actual worker result");
      ChunkMeshCaptureAdmissionTestAccess::Apply(cache, world, registry,
                                                 std::move(unsupported[0]));
      Expect(cache.GetPublishCount(root) == 0 &&
                 ChunkMeshCaptureAdmissionTestAccess::TracksSource(cache, root,
                                                                   revision),
             "physical mesh cannot publish through a receiver without field "
             "support");
    }
  }
}

static void
TestPhysicalGeometryCapture(cutum::UBlockRegistry &registry,
                            const cutum::UBlockDefinitionStorage *definitions,
                            bool use_gpu = false)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  const glm::ivec3 root(0);
  world.GetChunkManager().EnsureChunk({1, 1, 1});
  for (int z = 4; z <= 11; ++z)
    for (int x = 4; x <= 11; ++x)
      world.SetBlock({x, 4, z}, kStone);
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  const auto capture = [&](MeshLightInputs policy)
  {
    return ChunkMeshSnapshot::Capture(world, root, 7, nullptr, nullptr, policy);
  };
  auto physical = capture(MeshLightInputs::PhysicalField);
  const auto baked = capture(MeshLightInputs::Baked);
  Expect(physical.lightInputs == MeshLightInputs::PhysicalField &&
             !physical.renderLight && !physical.certifiedPaddedLight &&
             physical.openSkyAbsentMask == 0 &&
             std::all_of(physical.paddedLight.begin(),
                         physical.paddedLight.end(),
                         [](uint8_t value) { return value == 0; }),
         "physical geometry capture owns no baked halo or field proof");
  for (size_t n = 0; n < physical.inputStamps.size(); ++n)
    Expect(!physical.inputStamps[n].readsLight &&
               physical.inputStamps[n].readsContent == (n < 7) &&
               physical.inputStamps[n].light == 0 &&
               physical.inputStamps[n].lightIncarnation == 0,
           "physical geometry capture stamps only its seven geometric inputs");
  const auto canonical = [](const std::vector<GreedyQuad> &quads)
  {
    std::vector<std::array<int, 11>> keys;
    for (const auto &q : quads)
      keys.push_back({q.axis, q.faceSign, q.slice, q.u, q.v, q.width, q.height,
                      q.Id, q.FluidPacked, q.FluidSurface, q.LightPacked});
    std::sort(keys.begin(), keys.end());
    return keys;
  };
  const auto before =
      UGreedyMesher::BuildChunkMesh(physical, registry, catalog.get());
  Expect(before.size() == 6, "physical slab has six merged geometric surfaces");
  auto *chunk = world.GetChunkManager().GetChunk(root);
  for (int z = 0; z < CHUNK_SIZE; ++z)
    for (int y = 0; y < CHUNK_SIZE; ++y)
      for (int x = 0; x < CHUNK_SIZE; ++x)
        chunk->SetLightLocal({x, y, z}, (x + z) % 2 ? 15 : 2, y % 3);
  auto changed = capture(MeshLightInputs::PhysicalField);
  Expect(
      physical.InputsStillValid(world) && !baked.InputsStillValid(world) &&
          canonical(UGreedyMesher::BuildChunkMesh(
              changed, registry, catalog.get())) == canonical(before),
      "light mutation preserves physical geometry validity and merge layout");
  const auto changed_baked = capture(MeshLightInputs::Baked);
  Expect(UGreedyMesher::BuildChunkMesh(changed_baked, registry, catalog.get())
                 .size() > before.size(),
         "baked compatibility reader still splits faces by light");
  world.GetChunkManager().GetChunk({1, 1, 1})->SetLightLocal({0, 0, 0}, 0, 14);
  Expect(physical.InputsStillValid(world) &&
             !changed_baked.InputsStillValid(world),
         "diagonal field changes do not invalidate physical geometry");
  changed.light_packed.fill(255);
  changed.paddedLight.fill(255);
  changed.openSkyAbsentMask = ~uint32_t{0};
  Expect(canonical(UGreedyMesher::BuildChunkMesh(
             changed, registry, catalog.get())) == canonical(before) &&
             canonical(MergeOpaqueQuadsStrict(ExtractOpaqueFacesCpu(
                 changed, registry))) == canonical(before),
         "physical CPU and GPU-host merges ignore all historical baked bytes");
#if defined(SECTION_LIGHT_BINDING_DRIVER)
  if (use_gpu)
  {
    UGpuGreedyMesher gpu;
    std::vector<GreedyMeshBatch> batches;
    Expect(gpu.TryExtractOpaqueToBatches(changed, registry, root, batches,
                                         false, true, catalog.get()),
           "physical geometry exercises actual GPU greedy vertex emission");
    size_t indices = 0;
    for (const auto &batch : batches)
    {
      indices += batch.indices.size();
      for (const auto &vertex : batch.vertices)
        Expect(vertex.skyLight == 0 && vertex.blockLight == 0,
               "physical GPU vertex emission does not bake historical light");
    }
    Expect(indices == 36,
           "physical GPU vertex emitter preserves six rectangles");
    UGpuMeshPipeline pipeline;
    Expect(pipeline.Init(1),
           "physical geometry initializes actual packed pipeline");
    const int slot = pipeline.GetAllocator().AllocateStagingSlot(false);
    UGpuMeshPipeline::GpuApplyTicket ticket;
    Expect(slot >= 0 && pipeline.KickComputePasses(changed, registry, root,
                                                   slot, ticket, catalog.get()),
           "physical geometry dispatches actual packed compute");
    glFinish();
    Expect(pipeline.TryCompleteCountersAndEmit(ticket, registry, 0) ==
               UGpuMeshPipeline::GpuFinishStatus::Ready,
           "physical packed counters and emission complete");
    glFinish();
    uint32_t count = 0;
    bool dark = false, lit = false;
    std::vector<GpuBlockDrawRange> ranges;
    Expect(
        pipeline.TryFinishComputePasses(ticket, registry, count, &ranges, &dark,
                                        &lit, 0) ==
                UGpuMeshPipeline::GpuFinishStatus::Ready &&
            count == 6 && !lit,
        "physical packed geometry ignores historical center and padded light");
    pipeline.GetAllocator().ReleaseStagingSlot(ticket.slotKey);
    Expect(glGetError() == GL_NO_ERROR,
           "physical geometry GPU paths have no GL errors");
  }
#else
  (void)use_gpu;
#endif
  world.SetBlock({5, 5, 5}, kStone);
  Expect(!physical.InputsStillValid(world),
         "physical geometry rejects real content edits");
  const auto current = capture(MeshLightInputs::PhysicalField);
  auto profile = world.GetVerticalProfile();
  ++profile.SeaLevelY;
  world.SetVerticalProfile(profile);
  Expect(!current.InputsStillValid(world),
         "physical geometry rejects profile identity changes");
  UBlockWorld seam;
  seam.SetFluidDefinitions(definitions);
  seam.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  seam.SetBlock({15, 8, 8}, kStone);
  seam.SetBlock({16, 8, 8}, kStone);
  const auto ordinary = ChunkMeshSnapshot::Capture(
      seam, root, 7, nullptr, nullptr, MeshLightInputs::PhysicalField);
  const auto hidden_peer = ChunkMeshSnapshot::Capture(
      seam, root, 7, +[](void *, glm::ivec3) { return false; }, nullptr,
      MeshLightInputs::PhysicalField);
  Expect(canonical(UGreedyMesher::BuildChunkMesh(ordinary, registry,
                                                 catalog.get())) ==
                 canonical(UGreedyMesher::BuildChunkMesh(hidden_peer, registry,
                                                         catalog.get())) &&
             UGreedyMesher::BuildChunkMesh(hidden_peer, registry, catalog.get())
                     .size() == 5,
         "physical geometry depends on stored neighbor content rather than "
         "peer visibility");
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
      Expect(
          face_count(UGreedyMesher::BuildChunkMesh(world, coord, registry,
                                                   catalog.get())) == 1 &&
              face_count(UGreedyMesher::BuildChunkMesh(opened, registry,
                                                       catalog.get())) == 1,
          "opening behind glass preserves the opaque surface in both readers");
    }
  Expect(mismatches == 0, "all six section seams preserve the live mesher "
                          "second-plane face decision");
}

#if defined(SECTION_LIGHT_BINDING_DRIVER)
class DeferredTestMesher final : public cutum::IUChunkMesher
{
public:
  const char *BackendName() const override { return "deferred-test"; }
  std::vector<cutum::GreedyQuad>
  BuildChunkMesh(const cutum::UBlockWorld &world, glm::ivec3 coord,
                 cutum::UBlockRegistry &registry,
                 const cutum::BlockDefinitionCatalog *catalog) override
  {
    return cutum::UGreedyMesher::BuildChunkMesh(world, coord, registry,
                                                catalog);
  }
  std::vector<cutum::GreedyQuad>
  BuildChunkMesh(const cutum::ChunkMeshSnapshot &snapshot,
                 cutum::UBlockRegistry &registry,
                 const cutum::BlockDefinitionCatalog *catalog) override
  {
    return cutum::UGreedyMesher::BuildChunkMesh(snapshot, registry, catalog);
  }
  bool CanDeferGpuExtract(const cutum::ChunkMeshSnapshot &,
                          cutum::UBlockRegistry &,
                          const cutum::BlockDefinitionCatalog *) const override
  {
    ++eligibility_calls;
    return true;
  }
  mutable int eligibility_calls{0};
};

static void TestOldGpuNewGpuOwner(cutum::UBlockRegistry &,
                                  const cutum::UBlockDefinitionStorage *);
static void TestOldGpuNewCpuOwner(cutum::UBlockRegistry &,
                                  const cutum::UBlockDefinitionStorage *);

static void
TestDenseCutoutFallback(cutum::GpuGreedyEmitState &state,
                        cutum::UBlockRegistry &registry,
                        const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  const glm::ivec3 coord(0, -3, 0);
  for (int y = 0; y < CHUNK_SIZE; ++y)
    for (int z = 0; z < CHUNK_SIZE; ++z)
      for (int x = 0; x < CHUNK_SIZE; ++x)
        world.SetBlock(coord * CHUNK_SIZE + glm::ivec3(x, y, z),
                       (x + y + z) % 2 ? kSolidCutout : kTransparentCutout);
  const auto snapshot = ChunkMeshSnapshot::Capture(world, coord, 1);
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  std::vector<GreedyMeshBatch> batches;
  Expect(!TryGpuOpaqueEmitToBatches(state, snapshot, registry, coord, batches,
                                    catalog.get()) &&
             batches.empty(),
         "dense cutout GPU rectangle overflow is rejected without partial "
         "geometry");
  const auto complete =
      UGreedyMesher::BuildChunkMesh(snapshot, registry, catalog.get());
  Expect(complete.size() == CHUNK_VOLUME * 6u,
         "CPU route preserves all 24576 checkerboard cutout faces after "
         "bounded GPU rejection");
  Expect(glGetError() == GL_NO_ERROR,
         "dense cutout rejection does not issue invalid GL operations");

  RelightJobSpec spec;
  spec.min_world_y = world.GetVerticalProfile().MinY;
  spec.max_world_y = world.GetVerticalProfile().MaxY;
  const auto light =
      UChunkRelightSnapshot::CaptureSections(world, spec, {coord})
          .Compute(registry);
  Expect(light.inputs_complete && light.chunks.size() == 1,
         "dense packed recovery has independent physical light inputs");
  world.GetChunkManager().GetChunk(coord)->ReplaceComputedLightVolume(
      *light.chunks[0].padded_light, true, true);
  Expect(world.RecordLightCalculation(light, catalog) == 1,
         "dense recovery installs current physical proof");
  DeferredTestMesher backend;
  UChunkMeshCache cache;
  auto settings = MakeFrustumMeshSettings();
  settings.AsyncMeshing = true;
  settings.GpuPackedMeshing = true;
  settings.FrustumCulling = false;
  cache.SetRenderSettings(settings);
  cache.SetMesherBackend(&backend);
  cache.SetMeshRebuildFocus(coord, 4, 1);
  const auto revision = cache.GetChunkMeshRevision(coord);
  const auto *chunk = world.GetChunkManager().GetChunk(coord);
  auto &demand = cache.GetRenderDemandStore();
  demand.NoteDemand(coord, revision, chunk->GetLightFieldRevision(), 0, 1.0,
                    cache.GetCaptureStore().WorldEpoch(),
                    chunk->GetIncarnation());
  demand.NoteLightCalculationSettled(
      coord, cache.GetCaptureStore().WorldEpoch(), chunk->GetIncarnation(),
      chunk->GetLightFieldRevision(), chunk->GetContentRevision());
  const auto current =
      ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, coord);
  Expect(current.certifiedPaddedLight,
         "packed overflow input carries a certified physical volume");
  ChunkMeshCaptureAdmissionTestAccess::QueuePacked(cache, current, registry);
  const auto held_slots = ChunkMeshCaptureAdmissionTestAccess::HoldAllPackedSlots(cache);
  ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache, world, registry);
  const bool slot_refusal_retained = held_slots.size() == 2 &&
      cache.GetPendingGpuAppliesCount() == 1 && cache.IsPendingGpuQueued(coord) &&
      cache.IsGpuExtractInFlight(coord) && cache.HasInflightMeshBuild(coord) &&
      cache.GetPublishCount(coord) == 0 &&
      ChunkMeshCaptureAdmissionTestAccess::FreePackedSlots(cache) == 0;
  Expect(slot_refusal_retained,
         "staging slot refusal preserves queued snapshot and exact active owner");
  ChunkMeshCaptureAdmissionTestAccess::ReleaseHeldPackedSlots(cache, held_slots);
  std::cout << "gpu_slot_refusal retained=" << slot_refusal_retained << '\n';

  for (int i = 0; i < 8 && cache.GetPendingGpuAppliesCount(); ++i)
  {
    ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache, world, registry);
    glFinish();
  }
  Expect(cache.GetPendingGpuAppliesCount() == 0 && cache.HasPendingDirty(),
         "actual packed overflow terminates with a recovery obligation");
  Expect(
      ChunkMeshCaptureAdmissionTestAccess::FreePackedSlots(cache) == 2,
      "failed actual GPU ticket releases its staging slot even when invalid");
  Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
             cache, world, coord, cache.GetChunkMeshRevision(coord))
             .has_value(),
         "overflow recovery can recapture current authoritative inputs");
  const auto retry =
      cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
  Expect(retry.Scheduled == 1 &&
             cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
         "overflow retry is submitted to the actual bounded worker queue");
  cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
  std::cout << "packed recovery drawable=" << cache.HasDrawableGreedyMesh(coord)
            << " vertices=" << cache.GetGreedyVertexCount()
            << " dirty=" << cache.HasPendingDirty()
            << " pending=" << cache.GetPendingGpuAppliesCount()
            << " eligibility=" << backend.eligibility_calls << '\n';
  Expect(cache.HasDrawableGreedyMesh(coord) &&
             cache.GetGreedyVertexCount() == CHUNK_VOLUME * 6u * 4u &&
             !cache.HasPendingDirty() &&
             cache.GetPendingGpuAppliesCount() == 0 &&
             backend.eligibility_calls == 0,
         "failed packed section recovers all faces on CPU without GPU retry");
  const auto published = cache.GetPublishCount(coord);
  ChunkMeshCaptureAdmissionTestAccess::QueuePacked(
      cache,
      ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, coord),
      registry);
  for (int i = 0; i < 8 && cache.GetPendingGpuAppliesCount(); ++i)
  {
    ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache, world, registry);
    glFinish();
  }
  Expect(cache.GetPendingGpuAppliesCount() == 0 &&
             cache.GetPublishCount(coord) == published &&
             cache.GetGreedyVertexCount() == CHUNK_VOLUME * 6u * 4u &&
             cache.HasDrawableGreedyMesh(coord) && cache.HasPendingDirty() &&
             ChunkMeshCaptureAdmissionTestAccess::FreePackedSlots(cache) == 2,
         "another actual packed overflow retains the complete drawable "
         "predecessor");
  cache.RemoveChunk(coord);
  Expect(!ChunkMeshCaptureAdmissionTestAccess::HasCpuRecovery(cache, coord),
         "eviction clears recovery routing instead of retaining coordinates");
  for (int i = 0; i < UGpuMeshPipeline::kReadbackRing + 4; ++i)
  {
    ChunkMeshCaptureAdmissionTestAccess::QueuePacked(
        cache,
        ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, coord),
        registry);
    ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache, world, registry);
    Expect(cache.GetPendingGpuAppliesCount() == 1 &&
               ChunkMeshCaptureAdmissionTestAccess::FreePackedSlots(cache) == 1,
           "eviction fixture holds a real dispatched staging/readback ticket");
    cache.RemoveChunk(coord);
    Expect(
        cache.GetPendingGpuAppliesCount() == 0 &&
            ChunkMeshCaptureAdmissionTestAccess::FreePackedSlots(cache) == 2,
        "repeated in-flight eviction releases staging and bounded PBO leases");
  }
  glFinish();
  Expect(glGetError() == GL_NO_ERROR,
         "packed overflow and CPU recovery issue no invalid GL operations");
}

static void TestGpuPendingByteAdmission(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  auto &credits = UPipelineAdmission::Get();
  const auto baseline = credits.GpuPendingBytes();
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
  world.SetBlock({8, 8, 8}, kStone);
  const glm::ivec3 root(0);
  DeferredTestMesher backend;
  UChunkMeshCache cache;
  auto settings = MakeFrustumMeshSettings();
  settings.AsyncMeshing = true;
  settings.GpuPackedMeshing = true;
  settings.FrustumCulling = false;
  settings.Lighting = LightingMode::Flat;
  cache.SetRenderSettings(settings);
  cache.SetMesherBackend(&backend);
  cache.SetMeshRebuildFocus(root, 4, 1);
  ChunkMeshCaptureAdmissionTestAccess::InitPacked(cache);
  cache.MarkDirtyPriority(root);
  UFlatLightingPipeline flat;
  flat.FillAllLoadedChunks(world);
  const auto revision = cache.GetChunkMeshRevision(root);
  const auto *chunk = world.GetChunkManager().GetChunk(root);
  const auto epoch = cache.GetCaptureStore().WorldEpoch();
  cache.GetRenderDemandStore().NoteDemand(root, revision, chunk->GetLightFieldRevision(),
      0, 1.0, epoch, chunk->GetIncarnation());
  cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
      chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
  UAsyncMeshBuilder builder(1);
  builder.SetMesher(&backend);
  const auto deliver = [&]() {
    auto input = ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, root);
    Expect(builder.Enqueue(input, registry) && builder.WaitIdleFor(std::chrono::seconds(5)),
           "GPU byte fixture actual worker produces deferred snapshot");
    auto results = builder.DrainCompleted(1);
    Expect(results.size() == 1 && results[0].GpuExtractPending,
           "GPU byte fixture result requests actual extraction");
    if (results.empty()) return;
    ChunkMeshCaptureAdmissionTestAccess::TrackSource(cache, root, revision);
    ChunkMeshCaptureAdmissionTestAccess::Apply(cache, world, registry, std::move(results[0]));
  };
  credits.SetGpuCap(baseline);
  deliver();
  const bool refused = cache.GetPendingGpuAppliesCount() == 0 &&
      cache.IsChunkMeshDirty(root) && !cache.HasInflightMeshBuild(root) &&
      cache.GetChunkMeshRevision(root) == revision && credits.GpuPendingBytes() == baseline;
  Expect(refused, "GPU byte refusal returns exact demand without ghost owner");
  // Clean the baseline's unexpected acceptance so the negative run is bounded.
  if (cache.GetPendingGpuAppliesCount()) cache.CancelAsyncInFlightKeepDirty();
  credits.SetGpuCap(256 * 1024 * 1024);
  deliver();
  const bool charged = cache.GetPendingGpuAppliesCount() == 1 && credits.GpuPendingBytes() > baseline;
  Expect(charged, "accepted pending GPU payload owns byte credit");
  for (int n = 0; n < 8 && cache.GetPendingGpuAppliesCount(); ++n)
  {
    ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache, world, registry);
    glFinish();
  }
  const bool current = cache.GetGpuMeshPipeline()->HasGpuMesh(root) &&
      cache.IsPublishedMeshCurrentForInputs(root, world) &&
      cache.GetPendingGpuAppliesCount() == 0 && credits.GpuPendingBytes() == baseline;
  Expect(current, "GPU byte successor publishes actual current GPU geometry and releases credit");
  std::cout << "gpu_byte_admission refused=" << refused << " charged=" << charged
            << " current=" << current << '\n';
}

static void TestGpuEnqueueQuotaLifetime(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  for (int mode = 0; mode < 3; ++mode)
  {
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
  world.SetBlock({8, 8, 8}, kStone);
  const glm::ivec3 root(0);
  DeferredTestMesher backend;
  UChunkMeshCache cache;
  auto settings = MakeFrustumMeshSettings();
  settings.AsyncMeshing = true;
  settings.GpuPackedMeshing = true;
  settings.FrustumCulling = false;
  settings.Lighting = LightingMode::Flat;
  cache.SetRenderSettings(settings);
  cache.SetMesherBackend(&backend);
  cache.SetMeshRebuildFocus(root, 4, 1);
  ChunkMeshCaptureAdmissionTestAccess::InitPacked(cache);
  cache.MarkDirtyPriority(root);
  UFlatLightingPipeline flat;
  flat.FillAllLoadedChunks(world);
  const auto initial_revision = cache.GetChunkMeshRevision(root);
  const auto *chunk = world.GetChunkManager().GetChunk(root);
  const auto epoch = cache.GetCaptureStore().WorldEpoch();
  cache.GetRenderDemandStore().NoteDemand(root, initial_revision, chunk->GetLightFieldRevision(),
      0, 1.0, epoch, chunk->GetIncarnation());
  cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
      chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
  const auto prepare = [&]() {
    flat.FillAllLoadedChunks(world);
    cache.GetRenderDemandStore().NoteDemand(root, cache.GetChunkMeshRevision(root),
        chunk->GetLightFieldRevision(), 0, 1.0, epoch, chunk->GetIncarnation());
    cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
        chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
    Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(cache, world, root,
        cache.GetChunkMeshRevision(root)).has_value(), "quota fixture captures current input");
  };
  const auto schedule = [&]() {
    prepare();
    const auto tick = cache.RebuildDirtyChunksWithStats(world, registry, 0, 8, false, 0);
    Expect(tick.Scheduled == 1 && cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
           "quota fixture actual cache worker finishes");
  };
  const auto drain = [&]() {
    cache.DrainAsyncMeshResults(world, registry, 8);
    for (int n = 0; n < 8 && cache.GetPendingGpuAppliesCount(); ++n)
    {
      cache.DrainPendingGpuMeshes(world, registry, 8, 1000);
      glFinish();
    }
  };
  if (mode != 0)
  {
    schedule();
    drain();
    Expect(cache.GetGpuMeshPipeline()->HasGpuMesh(root) &&
               cache.IsPublishedMeshCurrentForInputs(root, world),
           "quota fixture has actual current GPU predecessor");
    world.SetBlock({10, 8, 8}, kStone);
    cache.MarkDirtyPriority(root);
  }
  schedule();
  const auto revision = cache.GetChunkMeshRevision(root);
  const auto predecessor = cache.GetPublishCount(root);
  MeshWorkAdmission zero;
  zero.enqueue_gpu_budget = 0;
  cache.SetMeshWorkAdmission(zero);
  cache.SetEnterLitQuiesce(mode == 2);
  cache.DrainAsyncMeshResults(world, registry, 8);
  const bool retained = mode == 0
      ? cache.GetPendingGpuAppliesCount() == 1
      : cache.GetPendingGpuAppliesCount() == 0 && !cache.HasInflightMeshBuild(root) &&
          cache.GetPublishCount(root) == predecessor &&
          (mode == 2 ? cache.IsRemeshAfterApplyPending(root) : cache.IsChunkMeshDirty(root));
  Expect(retained && cache.GetChunkMeshRevision(root) == revision,
         "zero GPU enqueue quota retains remesh debt and allows FirstMesh");
  cache.SetEnterLitQuiesce(false);
  MeshWorkAdmission positive;
  positive.enqueue_gpu_budget = 8;
  cache.SetMeshWorkAdmission(positive);
  if (mode != 0)
  {
    // Real main tick promotes parked RAA; zero scheduling keeps this boundary observable.
    cache.RebuildDirtyChunksWithStats(world, registry, 0, 0, false, 0);
    Expect(cache.IsChunkMeshDirty(root), "quota release exposes executable dirty successor");
    schedule();
  }
  drain();
  const bool current = cache.GetGpuMeshPipeline()->HasGpuMesh(root) &&
      cache.IsPublishedMeshCurrentForInputs(root, world) &&
      cache.GetPublishCount(root) == predecessor + 1 &&
      !cache.IsChunkMeshDirty(root) && !cache.IsRemeshAfterApplyPending(root) &&
      cache.GetPendingGpuAppliesCount() == 0;
  Expect(current, "GPU enqueue quota successor publishes current without new dirty ingress");
  std::cout << "gpu_enqueue_quota mode=" << mode << " retained=" << retained
            << " current=" << current << '\n';
  }
}

static void TestGpuPendingCancellationLifetime(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  for (int mode = 0; mode < 7; ++mode)
    for (int phase = 0; phase < 3; ++phase)
  {
  const auto baseline = UPipelineAdmission::Get().GpuPendingBytes();
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
  world.SetBlock({8, 8, 8}, kStone);
  const glm::ivec3 root(0);
  DeferredTestMesher backend;
  UChunkMeshCache cache;
  auto settings = MakeFrustumMeshSettings();
  settings.AsyncMeshing = true;
  settings.GpuPackedMeshing = true;
  settings.FrustumCulling = false;
  settings.Lighting = LightingMode::Flat;
  cache.SetRenderSettings(settings);
  cache.SetMesherBackend(&backend);
  cache.SetMeshRebuildFocus(root, 4, 1);
  ChunkMeshCaptureAdmissionTestAccess::InitPacked(cache);
  cache.MarkDirtyPriority(root);
  UFlatLightingPipeline flat;
  flat.FillAllLoadedChunks(world);
  const auto initial_revision = cache.GetChunkMeshRevision(root);
  const auto *chunk = world.GetChunkManager().GetChunk(root);
  const auto epoch = cache.GetCaptureStore().WorldEpoch();
  cache.GetRenderDemandStore().NoteDemand(root, initial_revision, chunk->GetLightFieldRevision(),
      0, 1.0, epoch, chunk->GetIncarnation());
  cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
      chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
  const auto prepare = [&]() {
    flat.FillAllLoadedChunks(world);
    const auto current_epoch = cache.GetCaptureStore().WorldEpoch();
    cache.GetRenderDemandStore().NoteDemand(root, cache.GetChunkMeshRevision(root),
        chunk->GetLightFieldRevision(), 0, 1.0, current_epoch, chunk->GetIncarnation());
    cache.GetRenderDemandStore().NoteLightCalculationSettled(root, current_epoch,
        chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
    Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(cache, world, root,
        cache.GetChunkMeshRevision(root)).has_value(), "quota fixture captures current input");
  };
  const auto schedule = [&]() {
    prepare();
    const auto tick = cache.RebuildDirtyChunksWithStats(world, registry, 0, 8, false, 0);
    Expect(tick.Scheduled == 1 && cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
           "quota fixture actual cache worker finishes");
  };
  const auto drain = [&]() {
    cache.DrainAsyncMeshResults(world, registry, 8);
    for (int n = 0; n < 8 && cache.GetPendingGpuAppliesCount(); ++n)
    {
      cache.DrainPendingGpuMeshes(world, registry, 8, 1000);
      glFinish();
    }
  };
  schedule();
  cache.DrainAsyncMeshResults(world, registry, 8);
  Expect(cache.GetPendingGpuAppliesCount() == 1 &&
             UPipelineAdmission::Get().GpuPendingBytes() > baseline,
         "GPU cancellation starts with actual accepted charged pending result");
  if (phase > 0)
  {
    ChunkMeshCaptureAdmissionTestAccess::KickOnly(cache, registry);
    glFinish();
    if (phase == 2)
      Expect(ChunkMeshCaptureAdmissionTestAccess::AdvancePackedToKicked(cache, registry),
             "actual counter completion and emit establish exact Kicked phase");
  }
  const auto revision = cache.GetChunkMeshRevision(root);
  if (mode == 0) cache.CancelAsyncMeshWork();
  if (mode == 1) cache.CancelAsyncInFlightKeepDirty();
  if (mode == 2) cache.RemoveChunk(root);
  if (mode == 3) cache.CancelInFlightOutsideHorizontalRadius({4, 0, 0}, 0, -1);
  if (mode == 4) cache.CancelAsyncInFlightKeepDirty(root, 0);
  if (mode == 5) cache.MarkAllDirty();
  if (mode == 6)
  {
    const auto generation = cache.GetCaptureStore().CapturePolicyGeneration();
    settings.Lighting = LightingMode::Full;
    world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Full));
    cache.SetPrepareRenderLightFn([](uint64_t, const SectionGeometryRenderInputs &,
                                    const SectionRenderLightSnapshot &)
        -> std::shared_ptr<SectionRenderLightBinding> { return nullptr; });
    cache.SetRenderSettings(settings);
    cache.DrainPendingGpuMeshes(world, registry, 0, 0);
    Expect(cache.GetCaptureStore().LightInputsPolicy() == MeshLightInputs::PhysicalField &&
               cache.GetCaptureStore().CapturePolicyGeneration() != generation &&
               cache.GetCaptureStore().WorldEpoch() == epoch &&
               cache.GetPendingGpuAppliesCount() == 0 &&
               UPipelineAdmission::Get().GpuPendingBytes() == baseline &&
               cache.IsChunkMeshDirty(root),
           "actual policy transition retires pending payload and retains demand");
    settings.Lighting = LightingMode::Flat;
    world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
    cache.SetPrepareRenderLightFn({});
    cache.SetRenderSettings(settings);
    cache.DrainPendingGpuMeshes(world, registry, 0, 0);
    Expect(cache.GetCaptureStore().LightInputsPolicy() == MeshLightInputs::Baked &&
               cache.GetCaptureStore().CapturePolicyGeneration() != generation &&
               cache.GetCaptureStore().WorldEpoch() == epoch,
           "actual policy return cannot reuse old same-mode capture generation");
  }

  const bool protected_job = mode == 4;
  const bool boundary = protected_job
      ? cache.GetPendingGpuAppliesCount() == 1 && cache.HasInflightMeshBuild(root) &&
          UPipelineAdmission::Get().GpuPendingBytes() > baseline
      : cache.GetPendingGpuAppliesCount() == 0 && !cache.HasInflightMeshBuild(root) &&
          !cache.IsGpuExtractInFlight(root) &&
          UPipelineAdmission::Get().GpuPendingBytes() == baseline &&
          cache.IsChunkMeshDirty(root) == (mode == 1 || mode == 6);
  Expect(boundary, "GPU cancellation retires exact pending resource or preserves protected owner");
  if (!protected_job)
  {
    cache.DrainPendingGpuMeshes(world, registry, 8, 1000);
    Expect(cache.GetPublishCount(root) == 0, "cancelled GPU result cannot publish late");
    if (mode != 1 && mode != 6) cache.MarkDirtyPriority(root); // explicit new demand after abandonment
    else Expect(cache.GetChunkMeshRevision(root) == revision,
                "KeepDirty retains original request revision");
    schedule();
  }
  drain();
  const bool current = cache.GetGpuMeshPipeline()->HasGpuMesh(root) &&
      cache.IsPublishedMeshCurrentForInputs(root, world) &&
      cache.GetPendingGpuAppliesCount() == 0 && !cache.IsChunkMeshDirty(root) &&
      !cache.IsRemeshAfterApplyPending(root) &&
      UPipelineAdmission::Get().GpuPendingBytes() == baseline;
  Expect(current, "GPU cancellation current or protected successor publishes and releases bytes");
  std::cout << "gpu_pending_cancel mode=" << mode << " phase=" << phase
            << " boundary=" << boundary << " current=" << current << '\n';
  }
}

static void TestPackedLightArtifactOwnership(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  const glm::ivec3 root(0);
  world.SetBlock({14, 4, 14}, kStone);
  DeferredTestMesher backend;
  UChunkMeshCache cache;
  auto settings = MakeFrustumMeshSettings();
  settings.AsyncMeshing = false;
  settings.GpuPackedMeshing = true;
  settings.FrustumCulling = false;
  cache.SetRenderSettings(settings);
  cache.SetMesherBackend(&backend);
  cache.SetMeshRebuildFocus(root, 4, 1);
  cache.MarkDirty(root);
  SectionRenderLightBindings owner;
  bool reject_successor = false;
  cache.SetPrepareRenderLightFn(
      [&](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
          const SectionRenderLightSnapshot &light)
      {
        return reject_successor ? nullptr
                                : owner.PrepareForWorld(epoch, geometry, light);
      });
  const auto settle = [&]()
  {
    RelightJobSpec spec;
    spec.min_world_y = world.GetVerticalProfile().MinY;
    spec.max_world_y = world.GetVerticalProfile().MaxY;
    const auto field =
        UChunkRelightSnapshot::CaptureSections(world, spec, {root})
            .Compute(registry);
    Expect(field.inputs_complete && field.chunks.size() == 1,
           "packed artifact fixture computes complete physical field");
    world.GetChunkManager().ReplaceComputedSectionLightVolume(
        root, *field.chunks[0].padded_light, true, true);
    Expect(world.RecordLightCalculation(
               field, registry.GetDefinitionsCatalogSnapshot()) == 1,
           "packed artifact fixture records canonical physical proof");
    auto &demand = cache.GetRenderDemandStore();
    const auto *chunk = world.GetChunkManager().GetChunk(root);
    demand.NoteDemand(
        root, cache.GetChunkMeshRevision(root), chunk->GetLightFieldRevision(),
        0, 1.0, cache.GetCaptureStore().WorldEpoch(), chunk->GetIncarnation());
    demand.NoteLightCalculationSettled(
        root, cache.GetCaptureStore().WorldEpoch(), chunk->GetIncarnation(),
        chunk->GetLightFieldRevision(), chunk->GetContentRevision());
  };
  const auto flatten = [&]()
  { cache.UpdateVisibleInstances(Frustum{}, glm::mat4(1), glm::vec3(0)); };
  const auto pack = [&]()
  {
    auto snapshot =
        ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, root);
    ChunkMeshCaptureAdmissionTestAccess::QueuePacked(cache, std::move(snapshot),
                                                     registry);
    for (int n = 0; n < 8 && cache.GetPendingGpuAppliesCount(); ++n)
    {
      ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache, world, registry);
      glFinish();
    }
    Expect(cache.GetPendingGpuAppliesCount() == 0,
           "actual packed artifact pipeline completes");
    flatten();
  };
  const auto packed_bytes = [&]()
  {
    const auto &allocator = cache.GetGpuMeshPipeline()->GetAllocator();
    const auto *slot = allocator.GetSlot(root);
    Expect(slot && slot->QuadCount == 6,
           "actual packed artifact contains all six cube faces");
    std::vector<uint8_t> bytes(slot->QuadCount * sizeof(PackedQuad));
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, allocator.GetQuadSsbo());
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,
                       slot->OffsetQuads * sizeof(PackedQuad),
                       static_cast<GLsizeiptr>(bytes.size()), bytes.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return bytes;
  };
  settle();
  pack();
  // Published-field admission is mutually exclusive with a geometry owner.
  // Exercise the guard, not an impossible synthetic field/GPU overlap.
  const auto before_conflict = cache.GetPublishCount(root);
  auto older_geometry = ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, root);
  ChunkMeshCaptureAdmissionTestAccess::QueuePacked(cache, std::move(older_geometry), registry);
  world.SetBlock({16, 4, 16}, kFire);
  settle();
  const bool excluded = !cache.RequestPublishedLightRefresh(world, registry, root) &&
      !cache.IsPublishedLightRefreshPending(root) && cache.GetPendingGpuAppliesCount() == 1;
  Expect(excluded, "published field owner cannot overlap pending GPU geometry owner");
  for (int n=0;n<8 && cache.GetPendingGpuAppliesCount();++n) {
    ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache, world, registry);
    glFinish();
  }
  const bool compatible = cache.IsPublishedMeshCurrentForInputs(root, world) &&
      cache.GetPublishCount(root) == before_conflict + 1 &&
      cache.GetGpuMeshPipeline()->HasGpuMesh(root);
  Expect(compatible, "older physical GPU geometry publishes with current compatible field");
  const auto geometry_publication = cache.GetPublishCount(root);
  const auto geometry_bytes = packed_bytes();
  world.SetBlock({16, 4, 16}, BLOCK_AIR);
  settle();
  Expect(cache.RequestPublishedLightRefresh(world, registry, root),
         "field owner admits after geometry conflict has retired");
  cache.DrainPendingGpuMeshes(world, registry, 8, -1);
  flatten();
  const bool refreshed = !cache.IsPublishedLightRefreshPending(root) &&
      cache.IsPublishedMeshCurrentForInputs(root, world) &&
      cache.GetPublishCount(root) == geometry_publication && packed_bytes() == geometry_bytes &&
      !cache.IsChunkMeshDirty(root) && !cache.IsRemeshAfterApplyPending(root);
  Expect(refreshed, "actual field refresh completes without geometry publication or byte changes");
  ProbeCertifiedBinding(owner, cache.GetPublishedRenderLightBinding(root));
  std::cout << "gpu_field_owner excluded=" << excluded << " compatible=" << compatible
            << " refreshed=" << refreshed << '\n';
  std::cerr << "packed artifact refs=" << cache.GetGpuPackedOpaqueRefs().size()
            << " transparent=" << cache.GetGpuPackedTransparentRefs().size()
            << " resident=" << cache.GetGpuMeshPipeline()->HasGpuMesh(root)
            << " published_binding="
            << bool(cache.GetPublishedRenderLightBinding(root))
            << " dirty=" << cache.HasPendingDirty()
            << " revision=" << cache.GetChunkMeshRevision(root) << '\n';
  Expect(cache.GetGpuPackedOpaqueRefs().size() == 1,
         "actual GPU cube publishes a packed draw reference");
  auto original = cache.GetGpuPackedOpaqueRefs()[0].renderLightBinding;
  Expect(original && original == cache.GetPublishedRenderLightBinding(root),
         "packed draw initially owns its exact physical source");
  const auto original_words = original->ShaderWords();
  const auto original_slot = cache.GetGpuPackedOpaqueRefs()[0].slotIndex;
  const auto original_bytes = packed_bytes();
  const auto original_allocation =
      cache.GetGpuMeshPipeline()->GetAllocator().GetSlot(root)->generation;
  const auto original_publication = cache.GetPublishCount(root);
  cache.SetRepairGuardWorld(&world);
  for (bool emitter_present : {true, false})
  {
    world.SetBlock({16, 4, 16}, emitter_present ? kFire : BLOCK_AIR);
    settle();
    cache.MarkDirtyPriority(root,
                            MeshRevisionBumpReason::PriorityRelitInstallRepair);
    cache.DrainPendingGpuMeshes(world, registry, 8, -1);
    flatten();
    const auto current = world.CaptureRenderLightSnapshot(root);
    Expect(
        current &&
            original->Matches(cache.GetCaptureStore().WorldEpoch(),
                              original->GeometryInputs(), *current) &&
            original->ShaderWords() == original_words &&
            cache.GetPublishedRenderLightBinding(root) == original &&
            cache.GetPublishCount(root) == original_publication &&
            cache.GetGpuPackedOpaqueRefs().size() == 1 &&
            cache.GetGpuPackedOpaqueRefs()[0].slotIndex == original_slot &&
            cache.GetGpuMeshPipeline()
                    ->GetAllocator()
                    .GetSlot(root)
                    ->generation == original_allocation &&
            packed_bytes() == original_bytes &&
            cache.GetMeshedLightRevision(root) ==
                world.GetChunkManager().GetChunk(root)->GetLightFieldRevision(),
        "actual packed world/cache light update preserves SSBO bytes "
        "allocation and publication");
    ProbeCertifiedBinding(owner, original);
  }
  std::weak_ptr<SectionRenderLightBinding> weak_original = original;
  world.SetBlock({14, 4, 14}, BLOCK_AIR);
  world.SetBlock({13, 4, 14}, kStone);
  for (int x = -16; x < 32; ++x)
    for (int z = -16; z < 32; ++z)
      world.SetBlock({x, 31, z}, kStone);
  cache.MarkDirty(root);
  cache.GetCaptureStore().Invalidate(root);
  settle();
  reject_successor = true;
  cache.RebuildChunkImmediate(world, registry, root);
  flatten();
  Expect(cache.GetPublishedRenderLightBinding(root) == original &&
             cache.GetGeometryAwaitingLightCount() == 1 &&
             cache.GetPublishCount(root) == original_publication,
         "actual resource refusal retains packed predecessor and completed CPU "
         "geometry");
  Expect(cache.GetGpuPackedOpaqueRefs().size() == 1 &&
             cache.GetGpuPackedOpaqueRefs()[0].slotIndex == original_slot &&
             packed_bytes() == original_bytes,
         "resource refusal retains the actual complete previous packed slot");
  Expect(cache.GetGpuPackedOpaqueRefs()[0].renderLightBinding == original &&
             original->ShaderWords() == original_words && original->IsLive(),
         "retained packed draw keeps its own field rather than CPU successor "
         "field");
  original.reset();
  Expect(!weak_original.expired(),
         "live packed slot retains previous physical lease");
  reject_successor = false;
  cache.DrainPendingGpuMeshes(world, registry, 8, -1);
  flatten();
  const auto successor = cache.GetPublishedRenderLightBinding(root);
  Expect(successor && cache.GetGpuPackedOpaqueRefs().empty() &&
             !cache.GetGpuMeshPipeline()->HasGpuMesh(root) &&
             weak_original.expired() &&
             cache.GetGeometryAwaitingLightCount() == 0 &&
             cache.GetPublishCount(root) == original_publication + 1,
         "ready zero-field CPU geometry retires packed predecessor without "
         "another build");
  for (int x = -16; x < 32; ++x)
    for (int z = -16; z < 32; ++z)
      world.SetBlock({x, 31, z}, BLOCK_AIR);
  cache.MarkDirty(root);
  cache.GetCaptureStore().Invalidate(root);
  settle();
  cache.RebuildChunkImmediate(world, registry, root);
  flatten();
  Expect(
      cache.GetGpuPackedOpaqueRefs().empty() &&
          !cache.GetGpuMeshPipeline()->HasGpuMesh(root) &&
          weak_original.expired(),
      "visible lit CPU replacement retires packed slot and its source lease");
  pack();
  Expect(cache.GetGpuPackedOpaqueRefs().size() == 1 &&
             cache.GetGpuPackedOpaqueRefs()[0].renderLightBinding ==
                 cache.GetPublishedRenderLightBinding(root),
         "later actual packed publication binds its own successor field");
  auto final_binding = cache.GetGpuPackedOpaqueRefs()[0].renderLightBinding;
  std::weak_ptr<SectionRenderLightBinding> weak_final = final_binding;
  final_binding.reset();
  world.SetBlock({13, 4, 14}, BLOCK_AIR);
  cache.MarkDirty(root);
  cache.GetCaptureStore().Invalidate(root);
  pack();
  Expect(cache.GetGpuPackedOpaqueRefs().empty() &&
             !cache.GetGpuMeshPipeline()->HasGpuMesh(root) &&
             weak_final.expired() &&
             ChunkMeshCaptureAdmissionTestAccess::FreePackedSlots(cache) == 2,
         "actual zero-quad packed successor releases slot and physical field "
         "lease");
  Expect(glGetError() == GL_NO_ERROR,
         "packed artifact ownership fixture has no GL errors");
}
#endif

using FaceCells = std::set<std::tuple<int, int, int, int, int>>;
static FaceCells ExpandFaceCells(const std::vector<cutum::GreedyQuad> &quads)
{
  FaceCells cells;
  for (const auto &q : quads)
    for (int u = q.u; u < q.u + q.width; ++u)
      for (int v = q.v; v < q.v + q.height; ++v)
      {
        glm::ivec3 p;
        p[q.axis] = q.slice;
        p[(q.axis + 1) % 3] = u;
        p[(q.axis + 2) % 3] = v;
        cells.emplace(q.axis, q.faceSign, p.x, p.y, p.z);
      }
  return cells;
}

static void
TestMaterialVisibilityParity(cutum::UBlockRegistry &registry,
                             const cutum::UBlockDefinitionStorage *definitions,
                             bool gpu, bool gles = false)
{
  using namespace cutum;
  ChunkMeshSnapshot unsupported;
  unsupported.blocks[0] = kCubeLiquid;
  const auto pinned = registry.GetDefinitionsCatalogSnapshot();
  Expect(
      !SnapshotIsGpuExtractEligible(unsupported, registry) &&
          !SnapshotIsGpuExtractEligible(unsupported, pinned.get()),
      "physical liquid remains on the fluid path even with cube render style");
  unsupported.blocks[0] = kStone;
  unsupported.fluid_packed[0] =
      PackFluidCellState(FluidCellState::Source().WithKind(FluidKind::Water));
  Expect(!SnapshotIsGpuExtractEligible(unsupported, registry) &&
             !SnapshotIsGpuExtractEligible(unsupported, pinned.get()),
         "supplementary fluid geometry must not be lost through opaque GPU "
         "eligibility");
  int cases = 0, mismatches = 0;
  uint64_t physical_hash = 1469598103934665603ull;
  int physical_mismatches = 0;
  uint64_t canonical_hash = 1469598103934665603ull;
  const auto hash_value = [&](int value)
  {
    const auto bits = static_cast<uint32_t>(value);
    for (int shift = 0; shift < 32; shift += 8)
    {
      canonical_hash ^= (bits >> shift) & 255u;
      canonical_hash *= 1099511628211ull;
    }
  };
#if defined(SECTION_LIGHT_BINDING_DRIVER)
  GpuGreedyEmitState state;
  if (gpu && !gles)
    Expect(EnsureGpuOpaqueEmit(state),
           "actual production compute programs compile");
  if (gpu && gles)
  {
    const auto source = FaceMaskComputeSource(true);
    const auto *bytes = source.c_str();
    const auto shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &bytes, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
      char message[4096];
      glGetShaderInfoLog(shader, sizeof(message), nullptr, message);
      std::cerr << message << '\n';
    }
    Expect(ok == GL_TRUE,
           "production GLES mask source compiles on actual ES context");
    state.MaskProgram = glCreateProgram();
    glAttachShader(state.MaskProgram, shader);
    glLinkProgram(state.MaskProgram);
    glDeleteShader(shader);
    glGetProgramiv(state.MaskProgram, GL_LINK_STATUS, &ok);
    Expect(ok == GL_TRUE, "production GLES mask source links");
    glGenBuffers(1, &state.OccSsbo);
    glGenBuffers(1, &state.MaskSsbo);
  }
#endif
  for (auto source : {kStone, kSolidGlass, kSolidCutout, kTransparentCutout})
    for (auto neighbor :
         {BlockId(BLOCK_AIR), kStone, kSolidGlass, kSolidCutout,
          kTransparentCutout, kSolidCross, kWater, kCutoutLeaves})
      for (int backing = 0; backing < 4; ++backing)
        for (int axis = 0; axis < 3; ++axis)
          for (int sign : {-1, 1})
          {
            UBlockWorld world;
            world.SetFluidDefinitions(definitions);
            world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
            const glm::ivec3 coord(0, -3, 0);
            glm::ivec3 local(8), direction(0);
            local[axis] = sign < 0 ? 0 : CHUNK_SIZE - 1;
            direction[axis] = sign;
            const auto position = coord * CHUNK_SIZE + local;
            world.SetBlock(position, source);
            world.SetBlock(position + direction, neighbor);
            if (backing & 1)
              world.SetBlock(position - direction, kStone);
            if (backing & 2)
              world.SetBlock(position + 2 * direction, kStone);
            const auto snap = ChunkMeshSnapshot::Capture(world, coord, 1);
            const auto catalog = registry.GetDefinitionsCatalogSnapshot();
            const auto expected = ExpandFaceCells(UGreedyMesher::BuildChunkMesh(
                world, coord, registry, catalog.get()));
            Expect(expected == ExpandFaceCells(UGreedyMesher::BuildChunkMesh(
                                   snap, registry, catalog.get())),
                   "live/snapshot reference parity in material matrix");
            hash_value(cases);
            hash_value(static_cast<int>(expected.size()));
            for (const auto &cell : expected)
            {
              const auto [a, face_sign, x, y, z] = cell;
              for (int value : {a, face_sign, x, y, z})
                hash_value(value);
            }
            // Independent correction to the captured legacy oracle: every
            // opaque cube/cutout face behind a transparent solid must exist.
            // Collision occupancy and cells beyond that neighbor cannot make
            // the transparent material an opaque occluder.
            auto physical = expected;
            for (int z = 0; z < CHUNK_SIZE; ++z)
              for (int y = 0; y < CHUNK_SIZE; ++y)
                for (int x = 0; x < CHUNK_SIZE; ++x)
                {
                  const glm::ivec3 cell(x, y, z);
                  const auto id = snap.GetBlockLocal(cell);
                  if (id == BLOCK_AIR ||
                      CatalogIsTransparent(catalog.get(), id) ||
                      CatalogIsLiquid(catalog.get(), id))
                    continue;
                  for (int a = 0; a < 3; ++a)
                    for (int face_sign : {-1, 1})
                    {
                      glm::ivec3 offset(0);
                      offset[a] = face_sign;
                      const auto other =
                          snap.GetBlock(snap.ChunkOrigin() + cell + offset);
                      const auto style =
                          CatalogGetRenderStyle(catalog.get(), other);
                      if (CatalogIsTransparent(catalog.get(), other) &&
                          CatalogBlocksMovement(catalog.get(), other) &&
                          style != BlockRenderStyle::Cross &&
                          style != BlockRenderStyle::Fluid)
                        physical.emplace(a, face_sign, x, y, z);
                    }
                }
            const auto physical_value = [&](int value)
            {
              const auto bits = static_cast<uint32_t>(value);
              for (int shift = 0; shift < 32; shift += 8)
              {
                physical_hash ^= (bits >> shift) & 255u;
                physical_hash *= 1099511628211ull;
              }
            };
            physical_value(cases);
            physical_value(static_cast<int>(physical.size()));
            for (const auto &cell : physical)
            {
              const auto [a, fs, x, y, z] = cell;
              for (int value : {a, fs, x, y, z})
                physical_value(value);
            }
            FaceCells actual;
            if (!gpu)
              actual = ExpandFaceCells(ExtractOpaqueFacesCpu(snap, registry));
#if defined(SECTION_LIGHT_BINDING_DRIVER)
            else
            {
              std::vector<uint32_t> words, mask(CHUNK_VOLUME);
              BuildPaddedFaceVisibility(snap, registry, catalog.get(), words);
              glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.OccSsbo);
              glBufferData(GL_SHADER_STORAGE_BUFFER,
                           words.size() * sizeof(uint32_t), words.data(),
                           GL_DYNAMIC_DRAW);
              glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.MaskSsbo);
              glBufferData(GL_SHADER_STORAGE_BUFFER,
                           mask.size() * sizeof(uint32_t), nullptr,
                           GL_DYNAMIC_DRAW);
              glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, state.OccSsbo);
              glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, state.MaskSsbo);
              glUseProgram(state.MaskProgram);
              glUniform1ui(glGetUniformLocation(state.MaskProgram, "volume"),
                           CHUNK_VOLUME);
              glUniform1ui(glGetUniformLocation(state.MaskProgram, "side"),
                           CHUNK_SIZE);
              glUniform1ui(glGetUniformLocation(state.MaskProgram, "pad"),
                           kGpuFaceVisibilityPad);
              glDispatchCompute((CHUNK_VOLUME + 63) / 64, 1, 1);
              glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                              GL_BUFFER_UPDATE_BARRIER_BIT);
              const auto *mapped =
                  static_cast<const uint32_t *>(glMapBufferRange(
                      GL_SHADER_STORAGE_BUFFER, 0,
                      mask.size() * sizeof(uint32_t), GL_MAP_READ_BIT));
              Expect(mapped != nullptr,
                     "actual compute result maps for driver reference");
              std::copy_n(mapped, mask.size(), mask.begin());
              Expect(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER) == GL_TRUE,
                     "visibility readback remains valid");
              Expect(glGetError() == GL_NO_ERROR,
                     "actual compute visibility readback succeeds");
              for (int n = 0; n < CHUNK_VOLUME; ++n)
                for (int face = 0; face < 6; ++face)
                  if (mask[n] & (1u << face))
                    actual.emplace(face / 2, face % 2 ? 1 : -1, n % CHUNK_SIZE,
                                   n / (CHUNK_SIZE * CHUNK_SIZE),
                                   (n / CHUNK_SIZE) % CHUNK_SIZE);
            }
#endif
            if (actual != physical)
              ++physical_mismatches;
            ++cases;
            if (actual != expected)
            {
              if (++mismatches <= 4)
                std::cerr << "visibility mismatch gpu=" << gpu
                          << " source=" << source << " neighbor=" << neighbor
                          << " backing=" << backing << " axis=" << axis
                          << " sign=" << sign << '\n';
            }
          }
  std::cout << "material visibility cases=" << cases
            << " mismatches=" << mismatches << " gpu=" << gpu << '\n';
  // Frozen independently from the original live oracle plus the explicit
  // opaque-behind-transparent correction, before changing the shared rules.
  Expect(canonical_hash == 6223292245338193327ull,
         "material rules match the independent corrected visibility oracle");
  std::cout << "canonical_material_visibility_hash=" << canonical_hash << '\n';
  std::cout << "physical_visibility_hash=" << physical_hash
            << " physical_mismatches=" << physical_mismatches << '\n';
  Expect(physical_mismatches == 0,
         "transparent solid must not erase opaque surfaces");
  Expect(mismatches == 0,
         "GPU eligible material faces match canonical live mesher");
#if defined(SECTION_LIGHT_BINDING_DRIVER)
  if (gpu && !gles)
  {
    TestDenseCutoutFallback(state, registry, definitions);
    TestGpuPendingByteAdmission(registry, definitions);
    TestGpuEnqueueQuotaLifetime(registry, definitions);
    TestGpuPendingCancellationLifetime(registry, definitions);
    TestOldGpuNewCpuOwner(registry, definitions);
    TestOldGpuNewGpuOwner(registry, definitions);
    TestPackedLightArtifactOwnership(registry, definitions);
  }
#endif
}

static void TestFluidMapMutationDelivery(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  const glm::ivec3 cell(15, -33, 15), column(0);
  SetPreferGpuFluidColumnScan(true); // existing deterministic CPU scan backend
  for (const bool waterlogged : {false, true})
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetBlock(cell, waterlogged ? kTallGrass : kWater);
    if (waterlogged)
      world.SetFluidState(cell, FluidCellState::Flowing(3).WithKind(FluidKind::Water));
    UWorldMeshService service;
    const auto *before = service.GetCache().GetFluidSurfaceSlice(
        world, registry, column, cell.y);
    Expect(before && (waterlogged || before->HasSurface(15, 15)),
           "fluid map fixture caches a slice and represents ordinary water");
    if (waterlogged)
      world.SetFluidState(cell, {});
    else
      world.SetBlock(cell, BLOCK_AIR);
    service.NotifyFluidSurfaceDirtyAtBlock(world, &registry, cell);
    Expect(service.GetCache().GetFluidSurfaceDirtyGroundChunks().count(column),
           "last-fluid removal must invalidate its cached surface immediately");
    const auto *after = service.GetCache().GetFluidSurfaceSlice(
        world, registry, column, cell.y);
    Expect(after && !after->HasSurface(15, 15),
           "invalidated surface map removes the last fluid before a mesh rebuild");
  }
  UBlockWorld world;
  UWorldMeshService service;
  std::unordered_set<glm::ivec3, IVec3Hash> modified;
  const std::vector<glm::ivec3> changed{{15,-33,15}, {16,-32,15}, {15,-33,16}};
  service.MarkBlocksChunkDirtyBatchFromEdit(
      world, nullptr, changed, modified, true, false);
  for (const auto pos : changed)
  {
    const auto chunk = UChunkManager::WorldToChunk(pos);
    Expect(service.GetCache().GetFluidSurfaceDirtyGroundChunks().count(
               {chunk.x, 0, chunk.z}) && modified.count(chunk),
           "batch seam mutation retains both fluid-map and dirty-save obligations without immediate meshing");
  }
  SetPreferGpuFluidColumnScan(false);
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

static void TestNoDrawPhysicalResourceAdmission(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  const glm::ivec3 root(0);
  for (const bool hidden : {false, true})
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    world.GetChunkManager().EnsureChunk(root);
    if (hidden)
      for (auto coord : std::vector<glm::ivec3>{root,
                                                {-1, 0, 0},
                                                {1, 0, 0},
                                                {0, -1, 0},
                                                {0, 1, 0},
                                                {0, 0, -1},
                                                {0, 0, 1}})
        for (int z = 0; z < CHUNK_SIZE; ++z)
          for (int y = 0; y < CHUNK_SIZE; ++y)
            for (int x = 0; x < CHUNK_SIZE; ++x)
              world.SetBlock(coord * CHUNK_SIZE + glm::ivec3(x, y, z), kStone);
    const auto proof =
        UGreedyMesher::CaptureNoSurface(world, root, registry, catalog);
    Expect(bool(proof),
           "actual AIR/hidden-solid geometry obtains private NoSurface proof");
    Expect(!world.HasCompleteLightSamplingCalculation(root, catalog),
           "NoDraw fixture has no physical light completion");
    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.FrustumCulling = false;
    cache.SetRenderSettings(settings);
    int callbacks = 0;
    cache.SetPrepareRenderLightFn(
        [&](uint64_t, const SectionGeometryRenderInputs &,
            const SectionRenderLightSnapshot &)
            -> std::shared_ptr<SectionRenderLightBinding>
        {
          ++callbacks;
          return {};
        });
    auto snapshot =
        ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, root);
    Expect(!ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(
               cache, world, snapshot, registry),
           "empty-looking data without private geometry proof cannot skip "
           "physical resource admission");
    Expect(ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(
               cache, world, snapshot, registry, &*proof),
           "private NoSurface proof admits no-draw geometry without a light "
           "resource");
    auto stale = snapshot;
    stale.sourceWorldEpoch = 0;
    Expect(!ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(
               cache, world, stale, registry, &*proof),
           "NoDraw cannot bypass missing source epoch");
    cache.GetRenderDemandStore().RequestLightSample(
        root, cache.GetCaptureStore().WorldEpoch(), 5, 8);
    cache.RebuildChunkImmediate(world, registry, root);
    Expect(
        cache.GetRenderDemandStore().HasLightSampleDemand(
            root, cache.GetCaptureStore().WorldEpoch(), 5),
        "NoDraw publication preserves an active physical point consumer lease");
    Expect(cache.GetPublishedGeometryRenderInputs(root).has_value() &&
               !cache.GetPublishedRenderLightBinding(root) &&
               !cache.GetPublishedRenderLightSnapshot(root) &&
               !cache.HasDrawableGreedyMesh(root) && callbacks == 0,
           "synchronous NoDraw publication allocates no light and publishes no "
           "triangles");
    const auto old_epoch = snapshot.sourceWorldEpoch;
    cache.GetCaptureStore().BumpWorldEpoch();
    Expect(cache.GetCaptureStore().WorldEpoch() != old_epoch &&
               !ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(
                   cache, world, snapshot, registry, &*proof),
           "private NoDraw evidence cannot revive a previous world epoch");
    UChunkMeshCache packed;
    packed.SetRenderSettings(settings);
    packed.SetPrepareRenderLightFn(
        [&](uint64_t, const SectionGeometryRenderInputs &,
            const SectionRenderLightSnapshot &)
            -> std::shared_ptr<SectionRenderLightBinding>
        {
          ++callbacks;
          return {};
        });
    packed.MarkDirtyPriority(root);
    const auto packed_snapshot =
        ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(packed, world, root);
    Expect(ChunkMeshCaptureAdmissionTestAccess::CommitEmptyPacked(
               packed, world, registry, packed_snapshot) &&
               callbacks == 0 && !packed.GetPublishedRenderLightBinding(root),
           "packed zero-quad admission independently verifies actual NoSurface "
           "without allocating light");
    Expect(!world.HasCompleteLightSamplingCalculation(root, catalog),
           "NoDraw publication never certifies a physical field");
    const auto *record = cache.GetRenderDemandStore().Find(root);
    Expect(!record || !record->has_settled_light,
           "NoDraw publication cannot fabricate light settlement");
    snapshot =
        ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, root);
    auto wrong_geometry = snapshot;
    ++wrong_geometry.inputStamps[0].content;
    Expect(!ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(
               cache, world, wrong_geometry, registry, &*proof),
           "NoDraw proof must match exact seven geometry stamps");
    if (hidden)
    {
      UChunkMeshCache asynchronous;
      settings.AsyncMeshing = true;
      asynchronous.SetRenderSettings(settings);
      asynchronous.SetMeshRebuildFocus(root, 4, 1);
      asynchronous.SetPrepareRenderLightFn(
          [&](uint64_t, const SectionGeometryRenderInputs &,
              const SectionRenderLightSnapshot &)
              -> std::shared_ptr<SectionRenderLightBinding>
          {
            ++callbacks;
            return {};
          });
      asynchronous.MarkDirtyPriority(root);
      Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
                 asynchronous, world, root,
                 asynchronous.GetChunkMeshRevision(root))
                 .has_value(),
             "hidden NoDraw fixture captures owned async inputs");
      const auto scheduled = asynchronous.RebuildDirtyChunksWithStats(
          world, registry, 0, 1, false, 0);
      Expect(scheduled.Scheduled == 1 &&
                 asynchronous.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
             "actual async hidden-solid mesh job completes");
      asynchronous.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
      Expect(asynchronous.GetPublishedGeometryRenderInputs(root).has_value() &&
                 !asynchronous.GetPublishedRenderLightBinding(root) &&
                 callbacks == 0,
             "actual async NoDraw publication needs no physical allocation "
             "callback");
    }
    world.SetBlock({4, 4, 4}, hidden ? BLOCK_AIR : kStone);
    auto changed =
        ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, root);
    Expect(!ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(
               cache, world, changed, registry, &*proof),
           "old NoDraw proof rejects a changed exposed surface");
  }
  for (const BlockId block : {kStone, kTallGrass, kSolidCutout, kWater})
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    world.SetBlock({4, 4, 4}, block);
    Expect(!UGreedyMesher::CaptureNoSurface(world, root, registry, catalog),
           "visible cube sprite cutout and fluid cannot claim NoSurface");
    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.FrustumCulling = false;
    cache.SetRenderSettings(settings);
    cache.SetPrepareRenderLightFn(
        [](uint64_t, const SectionGeometryRenderInputs &,
           const SectionRenderLightSnapshot &)
            -> std::shared_ptr<SectionRenderLightBinding> { return {}; });
    cache.RebuildChunkImmediate(world, registry, root);
    Expect(!cache.GetPublishedGeometryRenderInputs(root) &&
               cache.GetGeometryAwaitingLightCount() == 1 &&
               cache.HasInflightMeshBuild(root) &&
               !cache.IsChunkMeshDirty(root),
           "drawable source without computed light retains completed geometry");
  }
  UBlockWorld unknown;
  unknown.SetFluidDefinitions(definitions);
  unknown.GetChunkManager().EnsureChunk(root);
  Expect(!UGreedyMesher::CaptureNoSurface(unknown, root, registry, catalog),
         "unknown neighbors cannot certify absent surfaces");
}

static void TestCachedPreviewAfterProofOnlyCompletion(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetLightingConfigurationIdentity(
      static_cast<uint64_t>(LightingMode::Full));
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  const glm::ivec3 root(0);
  world.SetBlock({4, 4, 4}, kStone);
  for (int x = -16; x < 32; ++x)
    for (int z = -16; z < 32; ++z)
      world.SetBlock({x, 31, z}, kStone);
  RelightJobSpec spec;
  spec.min_world_y = world.GetVerticalProfile().MinY;
  spec.max_world_y = world.GetVerticalProfile().MaxY;
  const auto field = UChunkRelightSnapshot::CaptureSections(world, spec, {root})
                         .Compute(registry);
  Expect(field.inputs_complete && field.chunks.size() == 1 &&
             field.chunks[0].padded_light,
         "proof-only cache fixture computes a real complete physical field");
  world.GetChunkManager().ReplaceComputedSectionLightVolume(
      root, *field.chunks[0].padded_light, true, true);
  Expect(!world.CaptureRenderLightSnapshot(root),
         "installed light bytes alone do not establish render proof");
  UMeshCaptureStore store;
  UMeshCaptureStore shell_store;
  Expect(shell_store.CaptureAndStore(world, root, 7).has_value(),
         "incremental shell fixture retains an unverified capture");
  const auto before = store.CaptureAndStore(world, root, 7);
  Expect(before && !before->certifiedPaddedLight && !before->renderLight &&
             before->provisionalLightPreview,
         "real unverified zero field is cached as provisional");
  Expect(store.TryGet(world, root, 7).has_value(),
         "unfinished proof does not force repeated capture");
  const auto stamp_before = world.CaptureInputStamp(root, true, true);
  Expect(
      world.RecordLightCalculation(
          field, registry.GetDefinitionsCatalogSnapshot()) == 1 &&
          world.CaptureRenderLightSnapshot(root),
      "recording completed calculation makes physical render proof available");
  const auto stamp_after = world.CaptureInputStamp(root, true, true);
  Expect(stamp_before.light == stamp_after.light &&
             stamp_before.lightIncarnation == stamp_after.lightIncarnation &&
             before->InputsStillValid(world),
         "proof-only completion leaves raw field stamps and captured inputs "
         "unchanged");
  const auto hit = store.TryGet(world, root, 7);
  Expect(
      !hit || (hit->certifiedPaddedLight && hit->renderLight &&
               !hit->provisionalLightPreview),
      "cached provisional snapshot cannot survive proof-only light completion");
  const auto refreshed = hit ? hit : store.CaptureAndStore(world, root, 7);
  Expect(refreshed && refreshed->certifiedPaddedLight &&
             refreshed->renderLight && !refreshed->provisionalLightPreview &&
             refreshed->sourceRevision == 7 &&
             refreshed->sourceWorldEpoch == store.WorldEpoch() &&
             refreshed->renderLight->MatchesGeometry(
                 refreshed->GeometryRenderInputs(
                     registry.GetDefinitionsCatalogSnapshot())),
         "recapture preserves source epoch and geometry with the real "
         "completed field");
  const auto shell = shell_store.RefreshIncrementalShell(world, root, 7, 1);
  Expect(shell && shell->renderLight && shell->certifiedPaddedLight &&
             !shell->provisionalLightPreview &&
             shell->sourceWorldEpoch == shell_store.WorldEpoch(),
         "incremental shell refresh also replaces proof-only stale preview");
  const auto evictions = store.StaleEntryEvictions();
  Expect(store.TryGet(world, root, 7).has_value() &&
             store.StaleEntryEvictions() == evictions,
         "completed immutable capture remains reusable");
}

class CountingPhysicalMesher final : public cutum::IUChunkMesher
{
public:
  bool deferred{false};
  bool extract_batches{false};
  std::atomic<unsigned> builds{0};
  const char *BackendName() const override { return "counting-physical-cpu"; }
  bool CanDeferGpuExtract(const cutum::ChunkMeshSnapshot &,
                          cutum::UBlockRegistry &,
                          const cutum::BlockDefinitionCatalog *) const override
  {
    return deferred;
  }
  std::vector<cutum::GreedyQuad>
  BuildChunkMesh(const cutum::UBlockWorld &world, glm::ivec3 coord,
                 cutum::UBlockRegistry &registry,
                 const cutum::BlockDefinitionCatalog *catalog) override
  {
    Count(coord);
    return cutum::UGreedyMesher::BuildChunkMesh(world, coord, registry,
                                                catalog);
  }
  std::vector<cutum::GreedyQuad>
  BuildChunkMesh(const cutum::ChunkMeshSnapshot &snapshot,
                 cutum::UBlockRegistry &registry,
                 const cutum::BlockDefinitionCatalog *catalog) override
  {
    Count(snapshot.coord);
    return cutum::UGreedyMesher::BuildChunkMesh(snapshot, registry, catalog);
  }
  unsigned BuildsFor(glm::ivec3 coord) const
  {
    const std::lock_guard<std::mutex> lock(CountsMutex);
    const auto it = BuildsByCoord.find(coord);
    return it == BuildsByCoord.end() ? 0 : it->second;
  }
  bool TryExtractOpaqueToBatches(
      const cutum::ChunkMeshSnapshot &snapshot, cutum::UBlockRegistry &registry,
      glm::ivec3 coord, std::vector<cutum::GreedyMeshBatch> &batches, bool,
      bool, const cutum::BlockDefinitionCatalog *catalog) override
  {
    if (!extract_batches)
      return false;
    batches.clear();
    std::unordered_map<cutum::BlockId, cutum::GreedyMeshBatch> by_id;
    for (const auto &quad : BuildChunkMesh(snapshot, registry, catalog))
    {
      auto &batch = by_id[quad.Id];
      batch.blockId = quad.Id;
      cutum::AppendGreedyQuad(quad, coord, batch.vertices, batch.indices);
    }
    for (auto &entry : by_id)
      batches.push_back(std::move(entry.second));
    return true;
  }

private:
  void Count(glm::ivec3 coord)
  {
    ++builds;
    const std::lock_guard<std::mutex> lock(CountsMutex);
    ++BuildsByCoord[coord];
  }
  mutable std::mutex CountsMutex;
  std::unordered_map<glm::ivec3, unsigned, cutum::IVec3Hash> BuildsByCoord;
};

static void TestEmptyPhysicalFluidPublication(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  const glm::ivec3 root(0);
  for (int lane = 0; lane < 3; ++lane)
    for (const bool initially_calculated : {false, true})
    {
      // Headless packed commit has no allocator slot to retain while waiting;
      // actual-slot retention is covered by the GPU lifecycle driver.
      if (lane == 2 && !initially_calculated)
        continue;
      UBlockWorld world;
      world.SetFluidDefinitions(definitions);
      world.SetLightingConfigurationIdentity(
          static_cast<uint64_t>(LightingMode::Full));
      world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      for (const glm::ivec3 coord :
           {root, glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
            glm::ivec3(0, -1, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, -1),
            glm::ivec3(0, 0, 1)})
        for (int z = 0; z < CHUNK_SIZE; ++z)
          for (int y = 0; y < CHUNK_SIZE; ++y)
            for (int x = 0; x < CHUNK_SIZE; ++x)
            {
              const auto pos = coord * CHUNK_SIZE + glm::ivec3(x, y, z);
              world.SetBlock(pos, kWater);
              world.SetFluidState(
                  pos, FluidCellState::Source().WithKind(FluidKind::Water));
            }
      const auto catalog = registry.GetDefinitionsCatalogSnapshot();
      Expect(
          UGreedyMesher::BuildChunkMesh(world, root, registry).empty() &&
              !UGreedyMesher::CaptureNoSurface(world, root, registry, catalog),
          "enclosed liquid emits zero quads without an opaque NoSurface proof");
      UChunkMeshCache cache;
      auto settings = MakeFrustumMeshSettings();
      settings.AsyncMeshing = lane == 1;
      settings.FrustumCulling = false;
      cache.SetRenderSettings(settings);
      CountingPhysicalMesher mesher;
      cache.SetMesherBackend(&mesher);
      cache.SetRepairGuardWorld(&world);
      unsigned resource_calls = 0;
      cache.SetPrepareRenderLightFn(
          [&](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
              const SectionRenderLightSnapshot &light)
          {
            ++resource_calls;
            auto binding = std::make_shared<TestRenderLightBinding>(
                epoch, geometry, light);
            binding->refreshable = true;
            return binding;
          });
      cache.MarkDirtyPriority(root);
      const auto snapshot =
          ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, root);
      const auto revision = cache.GetChunkMeshRevision(root);
      const auto install_actual_field = [&]()
      {
        world.GetChunkManager().GetChunk(root)->SetLightLocal({8, 8, 8}, 7, 7);
        RelightJobSpec spec;
        spec.min_world_y = world.GetVerticalProfile().MinY;
        spec.max_world_y = world.GetVerticalProfile().MaxY;
        const auto field =
            UChunkRelightSnapshot::CaptureSections(world, spec, {root})
                .Compute(registry);
        Expect(field.inputs_complete && field.chunks.size() == 1,
               "empty liquid fixture calculates complete real field inputs");
        world.GetChunkManager().ReplaceComputedSectionLightVolume(
            root, *field.chunks[0].padded_light, true, true);
        Expect(world.RecordLightCalculation(field, catalog) == 1,
               "empty liquid fixture installs actual field provenance");
        const auto *chunk = world.GetChunkManager().GetChunk(root);
        auto &demand = cache.GetRenderDemandStore();
        demand.NoteDemand(root, cache.GetChunkMeshRevision(root),
                          chunk->GetLightFieldRevision(), 0, 0.0,
                          cache.GetCaptureStore().WorldEpoch(),
                          chunk->GetIncarnation());
        demand.NoteLightCalculationSettled(
            root, cache.GetCaptureStore().WorldEpoch(), chunk->GetIncarnation(),
            chunk->GetLightFieldRevision(), chunk->GetContentRevision());
      };
      if (initially_calculated)
        install_actual_field();
      if (lane == 2)
      {
        Expect(
            ChunkMeshCaptureAdmissionTestAccess::CommitEmptyPacked(
                cache, world, registry, snapshot),
            "empty packed liquid pairs actual field before first publication");
        cache.ClearDirtyAndRemeshAfterApply(root);
      }
      else
      {
        cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
        Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
               "empty liquid actual mesh worker finishes");
        cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
      }
      if (!initially_calculated)
      {
        Expect(mesher.BuildsFor(root) == 1 &&
                   cache.IsGeometryAwaitingLight(root) &&
                   cache.GetPublishCount(root) == 0 && resource_calls == 0,
               "empty liquid geometry retains its owner until actual light");
        for (int tick = 0; tick < 4; ++tick)
          cache.RebuildDirtyChunksWithStats(world, registry, 8, 1, false, 0);
        Expect(mesher.BuildsFor(root) == 1 &&
                   cache.GetChunkMeshRevision(root) == revision,
               "empty liquid field wait cannot rebuild completed geometry");
        install_actual_field();
        cache.ConsumeGpuApplyBacklog(world, registry, 8, 0, 0);
      }
      const unsigned initial_builds = lane == 2 ? 0 : 1;
      Expect(mesher.BuildsFor(root) == initial_builds &&
                 cache.GetPublishCount(root) == 1 &&
                 cache.HasMeshSatisfyingColumnReady(root) &&
                 !cache.HasDrawableGreedyMesh(root) && resource_calls == 1 &&
                 cache.GetPublishedRenderLightBinding(root) &&
                 cache.GetPublishedRenderLightSnapshot(root) &&
                 cache.GetChunkMeshRevision(root) == revision &&
                 snapshot.inputStamps[0].light == 0,
             "calculated field publishes original empty liquid geometry once");
      const auto prior_binding = cache.GetPublishedRenderLightBinding(root);
      world.SetBlock({16, 8, 8}, BLOCK_AIR);
      world.SetFluidState({16, 8, 8}, FluidCellState{});
      Expect(!UGreedyMesher::BuildChunkMesh(world, root, registry).empty(),
             "neighbor opening exposes actual fluid faces");
      cache.MarkDirty(root);
      cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
      Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
             "fluid exposure replacement worker finishes");
      cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
      Expect(mesher.BuildsFor(root) == initial_builds + 1 &&
                 cache.IsGeometryAwaitingLight(root) &&
                 cache.GetPublishCount(root) == 1 &&
                 cache.GetPublishedRenderLightBinding(root) == prior_binding,
             "new fluid faces wait for their current field without losing "
             "predecessor");
      install_actual_field();
      cache.ConsumeGpuApplyBacklog(world, registry, 8, 0, 0);
      Expect(cache.GetPublishCount(root) == 2 &&
                 cache.HasDrawableGreedyMesh(root) && resource_calls == 2,
             "actual successor field publishes exposed fluid geometry");
    }
}

static void
TestNoSurfaceRepairStability(cutum::UBlockRegistry &registry,
                             const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  const glm::ivec3 root(0);
  for (const bool hidden : {false, true})
    for (int lane = 0; lane < 3; ++lane)
    {
      UBlockWorld world;
      world.SetFluidDefinitions(definitions);
      world.SetLightingConfigurationIdentity(
          static_cast<uint64_t>(LightingMode::Full));
      world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      world.GetChunkManager().EnsureChunk(root);
      if (hidden)
        for (const glm::ivec3 coord :
             {root, glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
              glm::ivec3(0, -1, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, -1),
              glm::ivec3(0, 0, 1)})
          for (int z = 0; z < CHUNK_SIZE; ++z)
            for (int y = 0; y < CHUNK_SIZE; ++y)
              for (int x = 0; x < CHUNK_SIZE; ++x)
                world.SetBlock(coord * CHUNK_SIZE + glm::ivec3(x, y, z),
                               kStone);
      // Install a real nonzero-revision calculation while physical captures
      // retain their light-free worker stamp, as in the native counterexample.
      world.GetChunkManager().GetChunk(root)->SetLightLocal({8, 8, 8}, 7, 7);
      RelightJobSpec spec;
      spec.min_world_y = world.GetVerticalProfile().MinY;
      spec.max_world_y = world.GetVerticalProfile().MaxY;
      const auto field =
          UChunkRelightSnapshot::CaptureSections(world, spec, {root})
              .Compute(registry);
      Expect(field.inputs_complete && field.chunks.size() == 1,
             "NoSurface repair fixture calculates actual complete light");
      world.GetChunkManager().ReplaceComputedSectionLightVolume(
          root, *field.chunks[0].padded_light, true, true);
      Expect(world.RecordLightCalculation(
                 field, registry.GetDefinitionsCatalogSnapshot()) == 1,
             "NoSurface repair fixture installs the actual field proof");
      UChunkMeshCache cache;
      auto settings = MakeFrustumMeshSettings();
      settings.AsyncMeshing = lane == 1;
      settings.FrustumCulling = false;
      cache.SetRenderSettings(settings);
      CountingPhysicalMesher mesher;
      cache.SetMesherBackend(&mesher);
      cache.SetRepairGuardWorld(&world);
      cache.SetPrepareRenderLightFn(
          [](uint64_t, const SectionGeometryRenderInputs &,
             const SectionRenderLightSnapshot &)
              -> std::shared_ptr<SectionRenderLightBinding> { return {}; });
      cache.MarkDirtyPriority(root);
      if (lane == 2)
      {
        const auto snapshot = ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(
            cache, world, root);
        Expect(snapshot.inputStamps[0].light == 0 &&
                   world.GetChunkManager()
                           .GetChunk(root)
                           ->GetLightFieldRevision() > 0,
               "packed NoSurface worker and actual field have different light "
               "stamps");
        Expect(ChunkMeshCaptureAdmissionTestAccess::CommitEmptyPacked(
                   cache, world, registry, snapshot),
               "packed NoSurface geometry publishes without a field resource");
        cache.ClearDirtyAndRemeshAfterApply(root);
      }
      else
      {
        cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
        Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
               "NoSurface actual geometry worker finishes");
        cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
      }
      const auto revision = cache.GetChunkMeshRevision(root);
      const auto publications = cache.GetPublishCount(root);
      cache.GetRenderDemandStore().RequestLightSample(
          root, cache.GetCaptureStore().WorldEpoch(), 5, 8);
      Expect(
          publications == 1 &&
              cache.IsPublishedMeshCurrentForInputs(root, world),
          "current NoSurface publication is independent of baked light stamps");
      const auto field_revision =
          world.GetChunkManager().GetChunk(root)->GetLightFieldRevision();
      world.GetChunkManager().GetChunk(root)->SetLightLocal({8, 8, 8}, 13, 11);
      Expect(world.GetChunkManager().GetChunk(root)->GetLightFieldRevision() >
                 field_revision,
             "NoSurface light-only control must actually advance the field");
      cache.ReconcilePublishedMeshInputs(world, 1);
      Expect(cache.IsPublishedMeshCurrentForInputs(root, world) &&
                 !cache.IsChunkMeshDirty(root) &&
                 cache.GetChunkMeshRevision(root) == revision &&
                 cache.GetPublishCount(root) == publications,
             "bounded reconciliation must not rebuild NoSurface for a light-only change");
      for (int tick = 0; tick < 4; ++tick)
      {
        cache.MarkDirtyPriority(
            root, MeshRevisionBumpReason::PriorityChunkEmergeRepair);
        cache.MarkDirty(root, MeshRevisionBumpReason::PriorityWorldCoreRepair);
        cache.MarkDirtyPriority(
            root, MeshRevisionBumpReason::PriorityRelitInstallRepair);
        cache.MarkDirtyPriority(root);
        cache.MarkDirty(root);
        Expect(!cache.IsChunkMeshDirty(root) &&
                   cache.GetChunkMeshRevision(root) == revision &&
                   cache.GetPublishCount(root) == publications,
               "repair hints cannot churn current NoSurface geometry");
      }
      Expect(
          !cache.GetPublishedRenderLightBinding(root) &&
              cache.GetRenderDemandStore().HasLightSampleDemand(
                  root, cache.GetCaptureStore().WorldEpoch(), 5),
          "NoSurface repair retirement preserves independent light consumers");
      world.SetFluidDefinitions(nullptr);
      Expect(!cache.IsPublishedMeshCurrentForInputs(root, world),
             "NoSurface repair proof requires the world's current catalog");
      UBlockDefinitionStorage other_definitions;
      world.SetFluidDefinitions(&other_definitions);
      Expect(!cache.IsPublishedMeshCurrentForInputs(root, world),
             "another installed catalog invalidates NoSurface repair evidence");
      cache.ReconcilePublishedMeshInputs(world, 1);
      Expect(cache.IsChunkMeshDirty(root),
             "bounded reconciliation must repair a NoSurface catalog mismatch");
      world.SetFluidDefinitions(definitions);
      // Restoring a catalog does not erase the admitted geometry obligation.
      // Republish through the same lane before testing a new neighbor edit.
      if (lane == 2)
      {
        const auto restored = ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(
            cache, world, root);
        Expect(ChunkMeshCaptureAdmissionTestAccess::CommitEmptyPacked(
                   cache, world, registry, restored),
               "restored catalog republishes certified packed NoSurface");
        cache.ClearDirtyAndRemeshAfterApply(root);
      }
      else
      {
        cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
        Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
               "catalog repair geometry worker finishes");
        cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
      }
      Expect(cache.IsPublishedMeshCurrentForInputs(root, world),
             "actual catalog repair publishes current NoSurface before the next edit");
      if (lane == 2)
      {
        UChunkMeshCache epoch_cache;
        epoch_cache.SetRenderSettings(settings);
        epoch_cache.SetPrepareRenderLightFn(
            [](uint64_t, const SectionGeometryRenderInputs &,
               const SectionRenderLightSnapshot &)
                -> std::shared_ptr<SectionRenderLightBinding> { return {}; });
        epoch_cache.MarkDirtyPriority(root);
        const auto snapshot = ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(
            epoch_cache, world, root);
        Expect(ChunkMeshCaptureAdmissionTestAccess::CommitEmptyPacked(
                   epoch_cache, world, registry, snapshot),
               "epoch fixture publishes certified NoSurface geometry");
        epoch_cache.ClearDirtyAndRemeshAfterApply(root);
        Expect(epoch_cache.IsPublishedMeshCurrentForInputs(root, world),
               "NoSurface epoch fixture starts with current evidence");
        epoch_cache.GetCaptureStore().BumpWorldEpoch();
        Expect(!epoch_cache.IsPublishedMeshCurrentForInputs(root, world),
               "NoSurface repair evidence cannot cross a capture epoch");
        epoch_cache.ReconcilePublishedMeshInputs(world, 1);
        Expect(epoch_cache.IsChunkMeshDirty(root),
               "bounded reconciliation must repair a NoSurface epoch mismatch");
      }
      world.SetBlock(hidden ? glm::ivec3(16, 8, 8) : glm::ivec3(4, 4, 4),
                     hidden ? BLOCK_AIR : kStone);
      Expect(!cache.IsPublishedMeshCurrentForInputs(root, world) &&
                 !UGreedyMesher::BuildChunkMesh(world, root, registry).empty(),
             "geometry edit invalidates NoSurface and exposes real faces");
      // No explicit dependency notification: the bounded fallback must retain
      // zero-quad publications as geometry dependencies in all three lanes.
      cache.ReconcilePublishedMeshInputs(world, 1);
      Expect(cache.IsChunkMeshDirty(root),
             "bounded reconciliation independently repairs stale NoSurface geometry");
      cache.MarkDirtyPriority(
          root, MeshRevisionBumpReason::PriorityChunkEmergeRepair);
      Expect(cache.IsChunkMeshDirty(root),
             "NoSurface retirement cannot erase a later geometry obligation");
    }
}

static void TestUnknownGeometryDefersPhysicalBuild(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  const glm::ivec3 root(0), missing(1, 0, 0);
  for (const bool asynchronous : {false, true})
    for (const bool reserved_remesh : {false, true})
      for (const bool neighbor_pressure : {false, true})
        for (const bool legacy_light_deferred : {false, true})
        {
          UBlockWorld world;
          world.SetFluidDefinitions(definitions);
          world.SetLightingConfigurationIdentity(
              static_cast<uint64_t>(LightingMode::Full));
          world.SetKnownOpenSkyAbsentQuery([&](glm::ivec3 coord)
                                           { return coord != missing; });
          world.SetBlock({15, 8, 8}, kStone);
          CountingPhysicalMesher mesher;
          UChunkMeshCache cache;
          auto settings = MakeFrustumMeshSettings();
          settings.AsyncMeshing = asynchronous;
          settings.FrustumCulling = false;
          cache.SetRenderSettings(settings);
          cache.SetMesherBackend(&mesher);
          if (!legacy_light_deferred)
            cache.SetMeshRebuildFocus(root, 4, 1);
          cache.SetDeferMeshUntilLitFn([legacy_light_deferred](glm::ivec3)
                                       { return legacy_light_deferred; });
          cache.SetPrepareRenderLightFn(
              [](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
                 const SectionRenderLightSnapshot &light)
              {
                auto binding = std::make_shared<TestRenderLightBinding>(
                    epoch, geometry, light);
                binding->refreshable = true;
                return binding;
              });
          if (legacy_light_deferred)
          {
            cache.SetOnSoftDeferHeldFn([](glm::ivec3) {});
            cache.MarkDirtyPriority(root);
            cache.HoldSoftDeferFirstMesh(root);
            Expect(cache.IsSoftDeferHeld(root),
                   "fixture starts with a legacy light-held first-mesh owner");
            cache.PrefetchMeshCapture(world, root, &registry);
            Expect(!cache.IsSoftDeferHeld(root) && cache.IsChunkMeshDirty(root),
                   "physical policy transfers legacy held target to geometry "
                   "work");
          }
          cache.MarkDirtyPriority(root);
          const auto revision = cache.GetChunkMeshRevision(root);
          for (int tick = 0; tick < 4; ++tick)
            cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
          if (mesher.BuildsFor(root) != 0 || !cache.IsChunkMeshDirty(root) ||
              cache.HasInflightMeshBuild(root) ||
              cache.GetChunkMeshRevision(root) != revision)
            std::cerr << "unknown geometry fixture async=" << asynchronous
                      << " legacy_deferred=" << legacy_light_deferred
                      << " builds=" << mesher.BuildsFor(root)
                      << " dirty=" << cache.IsChunkMeshDirty(root)
                      << " inflight=" << cache.HasInflightMeshBuild(root)
                      << " revision=" << cache.GetChunkMeshRevision(root)
                      << " expected_revision=" << revision << '\n';
          Expect(cache.GetCaptureStore().LightInputsPolicy() ==
                         MeshLightInputs::PhysicalField &&
                     mesher.BuildsFor(root) == 0 &&
                     cache.IsChunkMeshDirty(root) &&
                     !cache.HasInflightMeshBuild(root) &&
                     cache.GetPublishCount(root) == 0 &&
                     cache.GetChunkMeshRevision(root) == revision,
                 "unknown physical geometry keeps one queued target without "
                 "doomed "
                 "builds");
          world.SetBlock({16, 8, 8}, kStone);
          cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
          Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
                 "new resident geometry finishes actual worker");
          cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
          Expect(mesher.BuildsFor(root) == 1 &&
                     cache.IsGeometryAwaitingLight(root) &&
                     cache.GetPublishCount(root) == 0 &&
                     cache.GetChunkMeshRevision(root) == revision,
                 "data completion builds original target once before light is "
                 "calculated");
          Expect(
              cache.NeedsPhysicalFieldInputColumn({1, 0, 1}) &&
                  cache.NeedsPhysicalFieldInputColumn({-1, 0, -1}) &&
                  !cache.NeedsPhysicalFieldInputColumn({2, 0, 0}) &&
                  !cache.NeedsPhysicalFieldInputColumn({0, 1, 0}),
              "current first-publication field owner prioritizes exactly its "
              "horizontal data halo");
          RelightJobSpec spec;
          spec.min_world_y = world.GetVerticalProfile().MinY;
          spec.max_world_y = world.GetVerticalProfile().MaxY;
          const auto field =
              UChunkRelightSnapshot::CaptureSections(world, spec, {root})
                  .Compute(registry);
          Expect(field.inputs_complete && field.chunks.size() == 1,
                 "resolved geometry supplies actual complete physical "
                 "calculation");
          world.GetChunkManager().ReplaceComputedSectionLightVolume(
              root, *field.chunks[0].padded_light, true, true);
          Expect(world.RecordLightCalculation(
                     field, registry.GetDefinitionsCatalogSnapshot()) == 1,
                 "resolved data installs actual private field proof");
          const auto *chunk = world.GetChunkManager().GetChunk(root);
          const auto epoch = cache.GetCaptureStore().WorldEpoch();
          cache.GetRenderDemandStore().NoteDemand(
              root, revision, chunk->GetLightFieldRevision(), 0, 1.0, epoch,
              chunk->GetIncarnation());
          cache.GetRenderDemandStore().NoteLightCalculationSettled(
              root, epoch, chunk->GetIncarnation(),
              chunk->GetLightFieldRevision(), chunk->GetContentRevision());
          cache.ConsumeGpuApplyBacklog(world, registry, 8, 0, 0);
          Expect(
              mesher.BuildsFor(root) == 1 && cache.GetPublishCount(root) == 1 &&
                  cache.GetGreedyVertexCount() == 20 &&
                  cache.GetGeometryAwaitingLightCount() == 0 &&
                  cache.GetPublishedRenderLightBinding(root) &&
                  !cache.HasProvisionalLightPreview(root) &&
                  !cache.ChunkHasStaleDarkFaces(root, world) &&
                  cache.GetChunkMeshRevision(root) == revision,
              "resident undrawn neighbor hides shared face and current field "
              "publishes original geometry once");
          Expect(!cache.NeedsPhysicalFieldInputColumn({1, 0, 1}),
                 "published field consumer cannot leave a data priority owner "
                 "behind");
          const auto previous_binding =
              cache.GetPublishedRenderLightBinding(root);
          const auto previous_generation =
              cache.GetCaptureStore().CapturePolicyGeneration();
          settings.Lighting = LightingMode::Flat;
          cache.SetRenderSettings(settings);
          cache.PrefetchMeshCapture(world, root, &registry);
          Expect(cache.GetCaptureStore().LightInputsPolicy() ==
                         MeshLightInputs::Baked &&
                     cache.GetCaptureStore().CapturePolicyGeneration() !=
                         previous_generation &&
                     cache.GetCaptureStore().WorldEpoch() == epoch &&
                     cache.IsChunkMeshDirty(root) &&
                     !cache.HasInflightMeshBuild(root) &&
                     cache.GetPublishedRenderLightBinding(root) ==
                         previous_binding &&
                     cache.GetPublishCount(root) == 1 &&
                     cache.GetChunkMeshRevision(root) == revision,
                 "physical to flat transition queues same target and preserves "
                 "predecessor ownership");
          settings.Lighting = LightingMode::Full;
          cache.SetRenderSettings(settings);
          cache.PrefetchMeshCapture(world, root, &registry);
          Expect(
              cache.GetCaptureStore().LightInputsPolicy() ==
                      MeshLightInputs::PhysicalField &&
                  cache.GetCaptureStore().WorldEpoch() == epoch &&
                  cache.IsChunkMeshDirty(root) &&
                  !cache.HasInflightMeshBuild(root) &&
                  cache.GetPublishedRenderLightBinding(root) ==
                      previous_binding &&
                  mesher.BuildsFor(root) == 1 &&
                  cache.GetChunkMeshRevision(root) == revision,
              "return to physical mode preserves original source and drawable "
              "until replacement");
          if (legacy_light_deferred)
          {
            Expect(
                cache.RequestPublishedLightRefresh(world, registry, root),
                "current geometry settles policy-transition retry before edit");
            world.SetBlock({8, 8, 8}, kStone);
            // Isolate enter-light quiesce from unrelated missing-neighbor
            // pressure.
            cache.SetMeshRebuildFocus(
                neighbor_pressure ? root : glm::ivec3(-1, 0, 0), 1, 2);
            cache.BeginHoleQueryFrame(neighbor_pressure ? root
                                                        : glm::ivec3(-1, 0, 0));
            MeshWorkAdmission admission;
            admission.protect_lit_settle_remesh = reserved_remesh;
            cache.SetMeshWorkAdmission(admission);
            cache.SetEnterLitQuiesce(true);
            cache.MarkDirty(root);
            const auto edited_revision = cache.GetChunkMeshRevision(root);
            if (neighbor_pressure)
            {
              for (int tick = 0; tick < 4; ++tick)
              {
                cache.RebuildDirtyChunksWithStats(world, registry, 8, 1, false,
                                                  0);
                Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
                       "admitted edit work drains under missing-neighbor "
                       "pressure");
                Expect(cache.IsChunkMeshDirty(root) ||
                           cache.HasInflightMeshBuild(root) ||
                           cache.IsGeometryAwaitingLight(root),
                       "missing-neighbor admission retains physical geometry "
                       "work");
                Expect(cache.GetPublishCount(root) == 1 &&
                           cache.GetPublishedRenderLightBinding(root) ==
                               previous_binding,
                       "admission pressure cannot replace the previous field "
                       "pair");
              }
              cache.SetMeshRebuildFocus({-1, 0, 0}, 1, 6);
              cache.BeginHoleQueryFrame({-1, 0, 0});
              if (asynchronous)
              {
                auto no_remesh = admission;
                no_remesh.enforce_schedule_lanes = true;
                no_remesh.max_schedule = 1;
                no_remesh.first_mesh_schedule = 1;
                no_remesh.remesh_schedule = 0;
                cache.SetMeshWorkAdmission(no_remesh);
                cache.SetMeshRebuildFocus(root, 1, 6);
                cache.BeginHoleQueryFrame(root);
                for (int tick = 0; tick < 2; ++tick)
                  cache.RebuildDirtyChunksWithStats(world, registry, 8, 1,
                                                    false, 0);
                Expect(cache.IsChunkMeshDirty(root) &&
                           mesher.BuildsFor(root) == 1,
                       "missing-neighbor zero quota retains physical geometry "
                       "work");
                cache.SetMeshRebuildFocus({-1, 0, 0}, 1, 8);
                cache.BeginHoleQueryFrame({-1, 0, 0});
                const auto queued_age = cache.GetDirtyQueueAgeFrames(root);
                for (int tick = 0; tick < 2; ++tick)
                  cache.RebuildDirtyChunksWithStats(world, registry, 8, 1,
                                                    false, 0);
                Expect(cache.IsChunkMeshDirty(root) &&
                           cache.GetDirtyQueueAgeFrames(root) > queued_age &&
                           mesher.BuildsFor(root) == 1,
                       "zero remesh quota retains the aged physical edit "
                       "without executing it");
                cache.SetMeshWorkAdmission(admission);
              }
            }
            cache.RebuildDirtyChunksWithStats(world, registry, 8, 1, false, 0);
            Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
                   "geometry edit under legacy light quiesce finishes worker");
            cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
            if (mesher.BuildsFor(root) != 2 ||
                !cache.IsGeometryAwaitingLight(root) ||
                cache.GetPublishCount(root) != 1 ||
                cache.GetPublishedRenderLightBinding(root) !=
                    previous_binding ||
                cache.GetChunkMeshRevision(root) != edited_revision)
              std::cerr << "geometry edit fixture async=" << asynchronous
                        << " reserved_remesh=" << reserved_remesh
                        << " neighbor_pressure=" << neighbor_pressure
                        << " builds=" << mesher.BuildsFor(root)
                        << " awaiting_light="
                        << cache.IsGeometryAwaitingLight(root)
                        << " dirty=" << cache.IsChunkMeshDirty(root)
                        << " inflight=" << cache.HasInflightMeshBuild(root)
                        << " publishes=" << cache.GetPublishCount(root)
                        << " previous_binding="
                        << (cache.GetPublishedRenderLightBinding(root) ==
                            previous_binding)
                        << " revision=" << cache.GetChunkMeshRevision(root)
                        << " expected_revision=" << edited_revision << '\n';
            Expect(
                mesher.BuildsFor(root) == 2 &&
                    cache.IsGeometryAwaitingLight(root) &&
                    cache.GetPublishCount(root) == 1 &&
                    cache.GetPublishedRenderLightBinding(root) ==
                        previous_binding &&
                    cache.GetChunkMeshRevision(root) == edited_revision,
                "legacy light quiesce cannot erase a real geometry edit; old "
                "publication survives until the replacement field is current");
          }
        }
}

static void
TestScreenRayFirstPublication(cutum::UBlockRegistry &registry,
                              const cutum::UBlockDefinitionStorage *definitions,
                              int initial_lane = 0)
{
  using namespace cutum;
  const glm::ivec3 root(-5, -3, 3);
  const glm::ivec3 focus(-3, -3, 3);
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetLightingConfigurationIdentity(
      static_cast<uint64_t>(LightingMode::Full));
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  world.SetBlock(root * CHUNK_SIZE + glm::ivec3(8), kStone);
  std::vector<glm::ivec3> unrelated;
  for (int x = -4; x <= -2; ++x)
    for (int z = 2; z <= 4; ++z)
      for (int y = -3; y <= 8; ++y)
      {
        const glm::ivec3 coord(x, y, z);
        unrelated.push_back(coord);
        world.SetBlock(coord * CHUNK_SIZE + glm::ivec3(8), kStone);
      }
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  RelightJobSpec spec;
  spec.min_world_y = world.GetVerticalProfile().MinY;
  spec.max_world_y = world.GetVerticalProfile().MaxY;
  const auto calculation =
      UChunkRelightSnapshot::CaptureSections(world, spec, {root})
          .Compute(registry);
  Expect(calculation.inputs_complete && calculation.chunks.size() == 1,
         "first-publication ray fixture computes actual physical light");
  auto *chunk = world.GetChunkManager().GetChunk(root);
  chunk->ReplaceComputedLightVolume(*calculation.chunks[0].padded_light, true,
                                    true);
  Expect(world.RecordLightCalculation(calculation, catalog) == 1,
         "first-publication ray fixture installs current field proof");
  CountingPhysicalMesher mesher;
  UChunkMeshCache cache;
  auto settings = MakeFrustumMeshSettings();
  settings.AsyncMeshing = true;
  settings.FrustumCulling = false;
  cache.SetRenderSettings(settings);
  cache.SetMesherBackend(&mesher);
  cache.SetMeshSnapshotBudgetMs(1000);
  cache.SetMeshEmergeTotalBudgetMs(1000);
  MeshWorkAdmission admission;
  admission.enforce_schedule_lanes = true;
  admission.max_schedule = 1;
  admission.first_mesh_schedule = 1;
  admission.remesh_schedule = 0;
  cache.SetMeshWorkAdmission(admission);
  cache.SetPrepareRenderLightFn(
      [](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
         const SectionRenderLightSnapshot &light)
          -> std::shared_ptr<SectionRenderLightBinding>
      {
        return std::make_shared<TestRenderLightBinding>(epoch, geometry, light);
      });
  if (initial_lane == 0)
    cache.MarkDirtyPriority(root);
  else
  {
    cache.MarkDirty(root);
    if (initial_lane == 2)
      Expect(cache.PrioritizeVisibleLightRepairRemesh(root),
             "first-publication fixture starts with priority remesh ownership");
  }
  // Streaming can enqueue this slice before the camera focus reaches it.
  cache.SetMeshRebuildFocus(focus, 4, 1);
  int32_t queue_index = -1, queue_size = 0;
  Expect(cache.GetDirtyQueueTrace(root, queue_index, queue_size) ==
             (initial_lane == 0 ? 1 : (initial_lane == 1 ? 3 : 2)),
         "first-publication fixture retains its actual pre-focus queue class");
  for (const auto coord : unrelated)
    cache.MarkDirtyPriority(coord);
  const auto revision = cache.GetChunkMeshRevision(root);
  const auto dirty_count = cache.GetDirtyCount();
  auto &demand = cache.GetRenderDemandStore();
  demand.NoteDemand(root, revision, chunk->GetLightFieldRevision(), 0, 1.0,
                    cache.GetCaptureStore().WorldEpoch(),
                    chunk->GetIncarnation());
  demand.NoteLightCalculationSettled(
      root, cache.GetCaptureStore().WorldEpoch(), chunk->GetIncarnation(),
      chunk->GetLightFieldRevision(), chunk->GetContentRevision());
  auto paused_admission = admission;
  paused_admission.max_schedule = 0;
  paused_admission.first_mesh_schedule = 0;
  cache.SetMeshWorkAdmission(paused_admission);
  Expect(cache.RebuildDirtyChunksWithStats(world, registry, 0, 0, false, 0)
                     .Scheduled == 0 &&
             !cache.HasInflightMeshBuild(root),
         "zero FirstMesh quota leaves the existing queued witness unstarted");
  cache.SetMeshWorkAdmission(admission);
  const auto queue_age = cache.GetDirtyQueueAgeFrames(root);
  Expect(queue_age > 0, "ray witness fixture has a nonzero existing queue age");
  Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(cache, world,
                                                                 root, revision)
             .has_value(),
         "first-publication ray fixture captures actual current geometry");
  Expect(!cache.HasDrawableGreedyMesh(root) &&
             !cache.HasInflightMeshBuild(root),
         "first-publication ray witness has only an existing dirty owner");
  Expect(cache.PrioritizeScreenRayRepair(root),
         "screen-ray first-publication witness promotes its existing queued "
         "slice");
  Expect(cache.PrioritizeScreenRayRepair(root) &&
             cache.GetDirtyCount() == dirty_count &&
             cache.GetChunkMeshRevision(root) == revision &&
             cache.GetDirtyQueueAgeFrames(root) == queue_age &&
             cache.GetDirtyQueueTrace(root, queue_index, queue_size) == 1,
         "ray classification and duplicate hints preserve one owner, revision "
         "and age");
  const auto tick =
      cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
  Expect(tick.Scheduled == 1 && cache.HasInflightMeshBuild(root),
         "one bounded FirstMesh slot serves the exact ray slice before "
         "unrelated debt");
  for (int i = 0; i < 2000 && cache.GetPublishCount(root) == 0; ++i)
  {
    cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  Expect(mesher.builds == 1 && cache.GetPublishCount(root) == 1 &&
             cache.HasDrawableGreedyMesh(root) &&
             cache.GetPublishedRenderLightBinding(root) &&
             cache.GetMeshPublishRevs(root).geom_rev == revision,
         "ray-prioritized first publication uses one actual worker and current "
         "physical field");
  cache.RemoveChunk(root);
  Expect(!cache.HasScreenRayRepairPin(root) &&
             !cache.PrioritizeScreenRayRepair(root),
         "evicted first-publication witness cannot create a detached repair "
         "owner");
}

class GatedPhysicalMesher final : public cutum::IUChunkMesher
{
public:
  std::atomic<unsigned> builds{0};
  std::atomic<bool> captured_physical{false};
  std::atomic<bool> timed_out{false};
  GatedPhysicalMesher()
      : entered_future(entered.get_future()),
        resume_future(resume.get_future().share())
  {
  }
  const char *BackendName() const override { return "gated-physical-cpu"; }
  bool WaitEntered()
  {
    return entered_future.wait_for(std::chrono::seconds(5)) ==
           std::future_status::ready;
  }
  void Release()
  {
    if (!released)
    {
      released = true;
      resume.set_value();
    }
  }
  std::vector<cutum::GreedyQuad>
  BuildChunkMesh(const cutum::UBlockWorld &world, glm::ivec3 coord,
                 cutum::UBlockRegistry &registry,
                 const cutum::BlockDefinitionCatalog *catalog) override
  {
    ++builds;
    return cutum::UGreedyMesher::BuildChunkMesh(world, coord, registry,
                                                catalog);
  }
  std::vector<cutum::GreedyQuad>
  BuildChunkMesh(const cutum::ChunkMeshSnapshot &snapshot,
                 cutum::UBlockRegistry &registry,
                 const cutum::BlockDefinitionCatalog *catalog) override
  {
    const bool first = builds.fetch_add(1) == 0;
    auto quads =
        cutum::UGreedyMesher::BuildChunkMesh(snapshot, registry, catalog);
    if (first)
    {
      captured_physical =
          snapshot.lightInputs == cutum::MeshLightInputs::PhysicalField &&
          !snapshot.certifiedPaddedLight && !snapshot.renderLight &&
          !snapshot.provisionalLightPreview &&
          std::none_of(snapshot.inputStamps.begin(), snapshot.inputStamps.end(),
                       [](const auto &stamp) { return stamp.readsLight; });
      entered.set_value();
      timed_out = resume_future.wait_for(std::chrono::seconds(5)) !=
                  std::future_status::ready;
    }
    return quads;
  }

private:
  std::promise<void> entered;
  std::future<void> entered_future;
  std::promise<void> resume;
  std::shared_future<void> resume_future;
  bool released{false};
};

static void TestSelectiveMeshLeaseWorkerLifecycle(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  // One worker: protected job queued behind a running outside job, with an
  // additional outside queued closure. Two workers: protected completion is
  // already ready when the cancelled outside worker is released late.
  for (const std::size_t workers : {1u, 2u})
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    const glm::ivec3 protected_coord(0), running_outside(2, 0, 0),
        queued_outside(3, 0, 0);
    for (const auto coord : {protected_coord, running_outside, queued_outside})
      world.SetBlock(coord * CHUNK_SIZE + glm::ivec3(8), kStone);
    auto &admission = UPipelineAdmission::Get();
    const auto snapshot_base = admission.SnapshotPendingBytes();
    const auto result_base = admission.ResultPendingBytes();
    const auto slots_base = admission.WorkSlotsInUse();
    GatedPhysicalMesher mesher;
    UAsyncMeshBuilder builder(workers);
    builder.SetMesher(&mesher);
    builder.SetCompletedCapacity(1);
    const auto submit = [&](glm::ivec3 coord, uint64_t revision) {
      return builder.Enqueue(ChunkMeshSnapshot::Capture(
          world, coord, revision, nullptr, nullptr,
          MeshLightInputs::PhysicalField), registry);
    };
    Expect(submit(running_outside, 801) && mesher.WaitEntered(),
           "selective lease barrier holds actual outside worker");
    Expect(submit(protected_coord, 802), "protected mesh job accepted");
    if (workers == 1)
      Expect(submit(queued_outside, 803), "outside queued mesh accepted");
    else
    {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (builder.GetCompletedSize() != 1 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
      Expect(builder.GetCompletedSize() == 1,
             "protected actual completion precedes outside release");
    }
    // Test-only negative control recreates the original whole-builder cancel.
    if (std::getenv("CUBA_TEST_FORCE_GLOBAL_MESH_CANCEL")) builder.CancelPending();
    else builder.CancelPendingOutsideHorizontalRadius({0, 0, 0}, 0);
    Expect(builder.IsInFlight(protected_coord) &&
               !builder.IsInFlight(running_outside) && !builder.IsInFlight(queued_outside),
           "selective cancellation preserves exact protected builder owner");
    if (workers == 1)
      Expect(admission.SnapshotPendingBytes() == snapshot_base + 2 * sizeof(ChunkMeshSnapshot) &&
                 admission.WorkSlotsInUse() == slots_base + 2,
             "outside queued cancellation releases exact snapshot and work-slot credits");
    mesher.Release();
    Expect(builder.WaitIdleFor(std::chrono::seconds(5)) && !mesher.timed_out,
           "selective workers finish after explicit barrier release");
    Expect(mesher.builds == 2 && builder.GetCompletedDiscardedOverflow() == 0 &&
               builder.GetCompletedSize() == 1 && builder.GetDiscardedLateJobMismatchCount() >= 1,
           "late outside result never displaces protected completion or computes queued outside job");
    auto results = builder.DrainCompleted(4);
    Expect(results.size() == 1 && results[0].coord == protected_coord &&
               results[0].sourceRevision == 802 && !results[0].batches.empty(),
           "original protected job yields real geometry with unchanged request identity");
    Expect(!builder.IsInFlight(protected_coord) &&
               admission.SnapshotPendingBytes() == snapshot_base &&
               admission.WorkSlotsInUse() == slots_base,
           "selective lifecycle retires worker owners and closure credits");
    results.clear();
    Expect(admission.ResultPendingBytes() == result_base,
           "selective completion releases its exact result credits");
    std::cout << "selective_mesh_lease_worker_case workers=" << workers << " PASS\n";
  }
}

static void TestCacheMeshRemovalLifetime(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  bool retired_all = true;
  bool published_all = true;
  for (const bool column : {true, false})
    for (const bool completed : {false, true})
    {
      UBlockWorld world;
      world.SetFluidDefinitions(definitions);
      world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
      world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      const glm::ivec3 root(0);
      world.SetBlock({8, 8, 8}, kStone);
      GatedPhysicalMesher mesher;
      if (completed) mesher.Release();
      UChunkMeshCache cache;
      auto settings = MakeFrustumMeshSettings();
      settings.AsyncMeshing = true;
      settings.FrustumCulling = false;
      settings.Lighting = LightingMode::Flat;
      cache.SetRenderSettings(settings);
      cache.SetMesherBackend(&mesher);
      cache.SetMeshRebuildFocus(root, 4, 1);
      const auto schedule = [&]() {
        UFlatLightingPipeline flat;
        flat.FillAllLoadedChunks(world);
        const auto *chunk = world.GetChunkManager().GetChunk(root);
        const auto revision = cache.GetChunkMeshRevision(root);
        const auto epoch = cache.GetCaptureStore().WorldEpoch();
        cache.GetRenderDemandStore().NoteDemand(root, revision, chunk->GetLightFieldRevision(),
            0, 1.0, epoch, chunk->GetIncarnation());
        cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
            chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
        Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
                   cache, world, root, revision).has_value(),
               "cache eviction fixture captures exact target");
        const auto tick = cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
        Expect(tick.Scheduled == 1 && cache.HasCpuMeshBuildInFlight(root),
               "cache eviction fixture owns exact actual worker");
      };
      cache.MarkDirtyPriority(root);
      schedule();
      Expect(mesher.WaitEntered(), "cache eviction worker reaches gate");
      if (completed)
        Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
               "cache eviction completed job awaits apply");
      if (column) cache.RemoveColumn(root, 0, 0);
      else cache.RemoveChunk(root);
      const bool owner_retired = !cache.HasCpuMeshBuildInFlight(root);
      mesher.Release();
      Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)) && !mesher.timed_out,
             "cache eviction releases actual worker");
      cache.DrainAsyncMeshResults(world, registry, 8);
      cache.DrainAsyncMeshResults(world, registry, 8);
      const bool clean = owner_retired && !cache.IsChunkMeshDirty(root) &&
          !cache.HasInflightMeshBuild(root) && !cache.HasDrawableGreedyMesh(root);
      retired_all = retired_all && clean;
      std::cout << "cache_mesh_removal column=" << column << " completed=" << completed
                << " owner_retired=" << owner_retired << " clean=" << clean << '\n';
      world.SetBlock({12, 8, 8}, kStone);
      cache.MarkDirtyPriority(root);
      schedule();
      Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
             "fresh cache demand completes actual worker after eviction");
      cache.DrainAsyncMeshResults(world, registry, 8);
      std::cout << "cache_remove_successor drawable=" << cache.HasDrawableGreedyMesh(root)
                << " current=" << cache.IsPublishedMeshCurrentForInputs(root, world)
                << " publications=" << cache.GetPublishCount(root)
                << " dirty=" << cache.IsChunkMeshDirty(root)
                << " active=" << cache.HasInflightMeshBuild(root)
                << " stale=" << cache.GetMeshApplyStaleCount()
                << " stale_rev=" << cache.GetMeshApplyStaleRevCount()
                << " no_active=" << cache.GetMeshApplyDropNoActiveCount() << '\n';
      published_all = published_all && cache.HasDrawableGreedyMesh(root) &&
                      cache.IsPublishedMeshCurrentForInputs(root, world);
    }
  Expect(retired_all, "cache removal retires exact CPU work without resurrecting dirty demand");
  Expect(published_all, "fresh cache demand after eviction publishes current geometry");
}

static void TestCacheMeshCancellationLifetime(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  bool retired_all = true;
  bool published_all = true;
  for (const bool keep_dirty : {false, true})
    for (const bool completed : {false, true})
    {
      UBlockWorld world;
      world.SetFluidDefinitions(definitions);
      world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
      world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      const glm::ivec3 root(0);
      world.SetBlock({8, 8, 8}, kStone);
      GatedPhysicalMesher mesher;
      if (completed) mesher.Release();
      UChunkMeshCache cache;
      auto settings = MakeFrustumMeshSettings();
      settings.AsyncMeshing = true;
      settings.FrustumCulling = false;
      settings.Lighting = LightingMode::Flat;
      cache.SetRenderSettings(settings);
      cache.SetMesherBackend(&mesher);
      cache.SetMeshRebuildFocus(root, 4, 1);
      const auto schedule = [&]() {
        UFlatLightingPipeline flat;
        flat.FillAllLoadedChunks(world);
        const auto *chunk = world.GetChunkManager().GetChunk(root);
        const auto revision = cache.GetChunkMeshRevision(root);
        const auto epoch = cache.GetCaptureStore().WorldEpoch();
        cache.GetRenderDemandStore().NoteDemand(root, revision, chunk->GetLightFieldRevision(),
            0, 1.0, epoch, chunk->GetIncarnation());
        cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
            chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
        Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
                   cache, world, root, revision).has_value(),
               "cache eviction fixture captures exact target");
        const auto tick = cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
        Expect(tick.Scheduled == 1 && cache.HasCpuMeshBuildInFlight(root),
               "cache eviction fixture owns exact actual worker");
      };
      cache.MarkDirtyPriority(root);
      schedule();
      Expect(mesher.WaitEntered(), "cache eviction worker reaches gate");
      if (completed)
        Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
               "cache eviction completed job awaits apply");
      if (keep_dirty) cache.CancelAsyncInFlightKeepDirty();
      else cache.CancelAsyncMeshWork();
      // Advance a real cache tick so old ScheduledThisFrame cannot hide a late retry.
      cache.RebuildDirtyChunksWithStats(world, registry, 0, 0, false, 0);
      const bool owner_retired = !cache.HasCpuMeshBuildInFlight(root);
      mesher.Release();
      Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)) && !mesher.timed_out,
             "cache eviction releases actual worker");
      cache.DrainAsyncMeshResults(world, registry, 8);
      cache.DrainAsyncMeshResults(world, registry, 8);
      const bool clean = owner_retired && cache.IsChunkMeshDirty(root) == keep_dirty &&
          !cache.HasInflightMeshBuild(root) && !cache.HasDrawableGreedyMesh(root);
      retired_all = retired_all && clean;
      std::cout << "cache_mesh_cancellation keep_dirty=" << keep_dirty << " completed=" << completed
                << " owner_retired=" << owner_retired << " clean=" << clean << '\n';
      if (!keep_dirty)
      {
        world.SetBlock({12, 8, 8}, kStone);
        cache.MarkDirtyPriority(root);
      } // KeepDirty successor must consume returned debt without new ingress.
      schedule();
      Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
             "fresh cache demand completes actual worker after eviction");
      cache.DrainAsyncMeshResults(world, registry, 8);
      std::cout << "cache_cancel_successor drawable=" << cache.HasDrawableGreedyMesh(root)
                << " current=" << cache.IsPublishedMeshCurrentForInputs(root, world)
                << " publications=" << cache.GetPublishCount(root)
                << " dirty=" << cache.IsChunkMeshDirty(root)
                << " active=" << cache.HasInflightMeshBuild(root)
                << " stale=" << cache.GetMeshApplyStaleCount()
                << " stale_rev=" << cache.GetMeshApplyStaleRevCount()
                << " no_active=" << cache.GetMeshApplyDropNoActiveCount() << '\n';
      published_all = published_all && cache.HasDrawableGreedyMesh(root) &&
                      cache.IsPublishedMeshCurrentForInputs(root, world);
    }
  Expect(retired_all, "cache cancellation preserves only the explicitly transferred dirty obligation");
  Expect(published_all, "cache cancellation successor publishes current geometry without invented debt");
}

static bool TestSameFrameDirtyCoalescing(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  bool all = true;
  for (int mode = 0; mode < 5; ++mode)
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    world.SetBlock({8, 8, 8}, kStone);
    const glm::ivec3 root(0);
    GatedPhysicalMesher mesher;
    if (mode != 3) mesher.Release();
    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.AsyncMeshing = true;
    settings.FrustumCulling = false;
    settings.Lighting = LightingMode::Flat;
    cache.SetRenderSettings(settings);
    cache.SetRepairGuardWorld(&world);
    cache.SetMesherBackend(&mesher);
    cache.SetMeshRebuildFocus(root, 4, 1);
    const auto schedule = [&](int budget = 1) {
      UFlatLightingPipeline flat;
      flat.FillAllLoadedChunks(world);
      const auto *chunk = world.GetChunkManager().GetChunk(root);
      const auto revision = cache.GetChunkMeshRevision(root);
      const auto epoch = cache.GetCaptureStore().WorldEpoch();
      cache.GetRenderDemandStore().NoteDemand(root, revision, chunk->GetLightFieldRevision(),
          0, 1.0, epoch, chunk->GetIncarnation());
      cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
          chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
      Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
                 cache, world, root, revision).has_value(), "coalescing fixture captures current inputs");
      return cache.RebuildDirtyChunksWithStats(world, registry, 0, budget, false, 0).Scheduled;
    };
    cache.MarkDirtyPriority(root);
    Expect(schedule() == 1 && mesher.WaitEntered(), "coalescing first job reaches actual worker");
    const auto initial_request = cache.GetChunkMeshRevision(root);
    if (mode != 3)
    {
      Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)), "coalescing predecessor computes");
      cache.DrainAsyncMeshResults(world, registry, 8);
      Expect(cache.IsPublishedMeshCurrentForInputs(root, world), "coalescing predecessor actually publishes current inputs");
    }
    world.SetBlock({12, 8, 8}, kStone);
    for (int hint = 0; hint < 6; ++hint)
    {
      if (mode == 0 || (mode == 2 && hint % 2 == 0)) cache.MarkDirty(root);
      else cache.MarkDirtyPriority(root);
    }
    if (mode == 3)
    {
      mesher.Release();
      Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)) && !mesher.timed_out,
             "active coalescing predecessor retires behind barrier");
      cache.DrainAsyncMeshResults(world, registry, 8);
    }
    const bool retained = cache.IsChunkMeshDirty(root) && !cache.HasCpuMeshBuildInFlight(root) &&
        !cache.HasInflightMeshBuild(root) && cache.GetDirtyCount() == 1;
    const bool revision_ok = cache.GetChunkMeshRevision(root) == initial_request + (mode == 3 ? 0 : 1);
    bool zero_retained = true;
    if (mode == 4)
    {
      MeshWorkAdmission zero;
      zero.enforce_schedule_lanes = true;
      zero.first_mesh_schedule = 1;
      zero.remesh_schedule = 0;
      cache.SetMeshWorkAdmission(zero);
      for (int tick = 0; tick < 2; ++tick)
      {
        const auto admitted = schedule();
        zero_retained = zero_retained && admitted == 0 && mesher.builds == 1 &&
            (cache.IsChunkMeshDirty(root) || cache.IsRemeshDeferred(root)) &&
            cache.GetChunkMeshRevision(root) == initial_request + 1;
      }
      cache.SetMeshWorkAdmission(MeshWorkAdmission{});
      std::cout << "baked_zero_quota retained=" << zero_retained << '\n';
    }
    auto retry_scheduled = schedule();
    const bool budget_retained = retry_scheduled == 1 || cache.IsChunkMeshDirty(root) ||
                                 cache.IsRemeshDeferred(root);
    if (retry_scheduled == 0 && budget_retained)
      retry_scheduled = schedule(8); // Lift admission only; do not create new dirty demand.
    Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)), "coalesced current successor computes");
    cache.DrainAsyncMeshResults(world, registry, 8);
    const bool current = retry_scheduled == 1 && cache.IsPublishedMeshCurrentForInputs(root, world) &&
        mesher.builds == 2 && cache.GetPublishCount(root) == (mode == 3 ? 1u : 2u);
    std::cout << "same_frame_publish mode=" << mode << " scheduled=" << retry_scheduled
              << " inputs_current=" << cache.IsPublishedMeshCurrentForInputs(root, world)
              << " builds=" << mesher.builds << " publications=" << cache.GetPublishCount(root)
              << " dirty=" << cache.IsChunkMeshDirty(root) << '\n';
    const auto publications = cache.GetPublishCount(root);
    for (int hint = 0; hint < 6; ++hint)
      cache.MarkDirtyPriority(root, MeshRevisionBumpReason::PriorityWorldCoreRepair);
    const bool no_op = !cache.IsChunkMeshDirty(root) && !cache.HasInflightMeshBuild(root) &&
        cache.GetPublishCount(root) == publications && mesher.builds == 2;
    std::cout << "same_frame_dirty mode=" << mode << " retained=" << retained
              << " revision=" << revision_ok << " budget_retained=" << budget_retained << " current=" << current << " no_op=" << no_op << '\n';
    all = all && retained && revision_ok && zero_retained && budget_retained && current && no_op;
  }
  return all;
}

static bool TestDeferredRemeshRetirement(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  bool all = true;
  for (int mode = 0; mode < 4; ++mode)
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    world.SetBlock({8, 8, 8}, kStone);
    const glm::ivec3 root(0);
    GatedPhysicalMesher mesher;
    mesher.Release();
    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.AsyncMeshing = true;
    settings.FrustumCulling = false;
    settings.Lighting = LightingMode::Flat;
    cache.SetRenderSettings(settings);
    cache.SetRepairGuardWorld(&world);
    cache.SetMesherBackend(&mesher);
    cache.SetMeshRebuildFocus(root, 4, 1);
    const auto schedule = [&]() {
      UFlatLightingPipeline flat;
      flat.FillAllLoadedChunks(world);
      const auto *chunk = world.GetChunkManager().GetChunk(root);
      const auto revision = cache.GetChunkMeshRevision(root);
      const auto epoch = cache.GetCaptureStore().WorldEpoch();
      cache.GetRenderDemandStore().NoteDemand(root, revision, chunk->GetLightFieldRevision(),
          0, 1.0, epoch, chunk->GetIncarnation());
      cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
          chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
      Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
                 cache, world, root, revision).has_value(), "deferred fixture captures actual current inputs");
      const auto tick = cache.RebuildDirtyChunksWithStats(world, registry, 0, 8, false, 0);
      Expect(tick.Scheduled == 1 && cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
             "deferred fixture computes exactly one actual job");
      cache.DrainAsyncMeshResults(world, registry, 8);
      Expect(cache.IsPublishedMeshCurrentForInputs(root, world),
             "deferred fixture publishes actual current geometry");
    };
    cache.MarkDirtyPriority(root);
    schedule();
    world.SetBlock({12, 8, 8}, kStone);
    cache.MarkDirty(root);
    const auto request = cache.GetChunkMeshRevision(root);
    // Exact existing producer transfer, isolated from quota-selection policy.
    for (int duplicate = 0; duplicate < 6; ++duplicate)
      ChunkMeshCaptureAdmissionTestAccess::ParkExistingRemesh(cache, root);
    Expect(cache.GetRemeshDeferredCount() == 1 && cache.IsRemeshDeferred(root) &&
               !cache.IsChunkMeshDirty(root) && cache.GetChunkMeshRevision(root) == request,
           "deferred transfer coalesces one exact existing obligation");
    if (mode == 1) cache.RemoveChunk(root);
    if (mode == 2) cache.RemoveColumn(root, 0, 0);
    if (mode == 3) cache.MarkAllDirty();
    if (mode == 1 || mode == 2) world.GetChunkManager().RemoveChunk(root);
    const bool cleared = mode == 0 || cache.GetRemeshDeferredCount() == 0;
    cache.BeginHoleQueryFrame({1, 0, 0});
    const bool outcome = cleared && !cache.IsRemeshDeferred(root) &&
        cache.IsChunkMeshDirty(root) == (mode == 0);
    std::cout << "deferred_remesh mode=" << mode << " cleared=" << cleared
              << " outcome=" << outcome << '\n';
    all = all && outcome;
    if (!outcome) continue; // Preserve failed retirement evidence; never repair it with fresh ingress.
    if (mode != 0)
    {
      world.SetBlock({8, 8, 8}, kStone);
      cache.MarkDirtyPriority(root);
    }
    schedule(); // mode0 consumes the returned existing request, without MarkDirty.
    all = all && mesher.builds == 2 && cache.GetRemeshDeferredCount() == 0 &&
        (mode != 0 || cache.GetChunkMeshRevision(root) == request);
  }
  return all;
}

static bool TestDeferredRemeshCapacity(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  std::vector<glm::ivec3> coords;
  for (int i = 0; i < 257; ++i)
  {
    coords.emplace_back(i * 2, 0, 0);
    world.SetBlock(coords.back() * CHUNK_SIZE + glm::ivec3(8), kStone);
  }
  GatedPhysicalMesher mesher;
  mesher.Release();
  UChunkMeshCache cache;
  auto settings = MakeFrustumMeshSettings();
  settings.AsyncMeshing = false;
  settings.FrustumCulling = false;
  settings.Lighting = LightingMode::Flat;
  cache.SetRenderSettings(settings);
  UFlatLightingPipeline flat;
  flat.FillAllLoadedChunks(world);
  for (const auto coord : coords)
  {
    cache.RebuildChunkImmediate(world, registry, coord);
    Expect(cache.HasDrawableGreedyMesh(coord), "deferred capacity fixture publishes real predecessor");
    world.SetBlock(coord * CHUNK_SIZE + glm::ivec3(12, 8, 8), kStone);
    cache.MarkDirty(coord);
  }
  for (const auto coord : coords)
    ChunkMeshCaptureAdmissionTestAccess::ParkExistingRemesh(cache, coord);
  for (int duplicate = 0; duplicate < 6; ++duplicate)
    ChunkMeshCaptureAdmissionTestAccess::ParkExistingRemesh(cache, coords.back());
  unsigned owned = 0;
  for (const auto coord : coords)
    owned += cache.IsChunkMeshDirty(coord) || cache.IsRemeshDeferred(coord);
  const bool preserved = cache.GetRemeshDeferredCount() <= 256 && owned == 257;
  std::cout << "deferred_capacity parked=" << cache.GetRemeshDeferredCount()
            << " owned=" << owned << " preserved=" << preserved << '\n';
  if (!preserved) return false; // Never rescue an evicted obligation with new ingress.
  // Explicitly retire the middle requests; isolate head and the over-cap tail
  // for actual execution without unrelated scheduler fairness assumptions.
  for (std::size_t i = 1; i + 1 < coords.size(); ++i)
  {
    cache.RemoveColumn(coords[i], 0, 0);
    world.GetChunkManager().RemoveChunk(coords[i]);
  }
  settings.AsyncMeshing = true;
  cache.SetRenderSettings(settings);
  cache.SetMesherBackend(&mesher);
  cache.SetMeshScheduleMaxHorizontalDist(0);
  cache.SetMeshScheduleOverflowPerFrame(0);
  MeshWorkAdmission admission;
  admission.enforce_schedule_lanes = true;
  admission.first_mesh_schedule = 0;
  admission.remesh_schedule = 1;
  cache.SetMeshWorkAdmission(admission);
  cache.BeginHoleQueryFrame({1, 0, 0});
  for (const auto coord : {coords.front(), coords.back()})
  {
    const auto request = cache.GetChunkMeshRevision(coord);
    Expect(cache.IsChunkMeshDirty(coord), "capacity edge request remains executable without new MarkDirty");
    cache.SetMeshRebuildFocus(coord, 1, 2);
    flat.FillAllLoadedChunks(world);
    const auto *chunk = world.GetChunkManager().GetChunk(coord);
    const auto epoch = cache.GetCaptureStore().WorldEpoch();
    cache.GetRenderDemandStore().NoteDemand(coord, request, chunk->GetLightFieldRevision(),
        0, 1.0, epoch, chunk->GetIncarnation());
    cache.GetRenderDemandStore().NoteLightCalculationSettled(coord, epoch,
        chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
    Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
               cache, world, coord, request).has_value(), "capacity edge captures actual current geometry");
    const auto tick = cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
    Expect(tick.Scheduled == 1 && cache.HasCpuMeshBuildInFlight(coord) &&
               cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
           "capacity edge owns exact actual current worker");
    cache.DrainAsyncMeshResults(world, registry, 8);
    Expect(cache.IsPublishedMeshCurrentForInputs(coord, world) &&
               cache.GetChunkMeshRevision(coord) == request && cache.GetPublishCount(coord) == 2,
           "capacity head and tail publish current inputs without replacing their request");
  }
  const bool complete = mesher.builds == 2 && cache.GetRemeshDeferredCount() == 0 &&
      !cache.HasPendingDirty();
  std::cout << "deferred_capacity edge_publications=" << mesher.builds << " complete=" << complete << '\n';
  return complete;
}

#if defined(SECTION_LIGHT_BINDING_DRIVER)
static void TestOldGpuNewGpuOwner(cutum::UBlockRegistry &registry,
                                  const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  for(int phase=0;phase<3;++phase) {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
    world.SetBlock({8,8,8},kStone);
    const glm::ivec3 root(0);
    DeferredTestMesher backend;
    UChunkMeshCache cache;
    auto settings=MakeFrustumMeshSettings();
    settings.AsyncMeshing=true; settings.GpuPackedMeshing=true;
    settings.FrustumCulling=false; settings.Lighting=LightingMode::Flat;
    cache.SetRenderSettings(settings); cache.SetMesherBackend(&backend);
    cache.SetMeshRebuildFocus(root,4,1);
    const auto capture=[&]() {
      UFlatLightingPipeline flat;flat.FillAllLoadedChunks(world);
      const auto *chunk=world.GetChunkManager().GetChunk(root);
      const auto epoch=cache.GetCaptureStore().WorldEpoch();
      auto &d=cache.GetRenderDemandStore();
      d.NoteDemand(root,cache.GetChunkMeshRevision(root),chunk->GetLightFieldRevision(),
          0,1.0,epoch,chunk->GetIncarnation());
      d.NoteLightCalculationSettled(root,epoch,chunk->GetIncarnation(),
          chunk->GetLightFieldRevision(),chunk->GetContentRevision());
      return ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache,world,root);
    };
    cache.MarkDirtyPriority(root);
    ChunkMeshCaptureAdmissionTestAccess::QueuePacked(cache,capture(),registry);
    if(phase>0) {
      ChunkMeshCaptureAdmissionTestAccess::KickOnly(cache,registry);glFinish();
      if(phase==2) Expect(ChunkMeshCaptureAdmissionTestAccess::AdvancePackedToKicked(cache,registry),
          "old GPU reaches Kicked before new GPU owner");
    }
    world.SetBlock({12,8,8},kStone);
    cache.InvalidateInFlightMeshBuild(root);
    auto newer=capture();
    Expect(cache.GetCaptureStore().TryCommit(root,newer.sourceRevision,
        newer.sourceWorldEpoch,newer,nullptr),"new GPU owner retains current capture");
    Expect(ChunkMeshCaptureAdmissionTestAccess::AdmitCpuThenAdvanceReceiptFrame(cache,newer,registry) &&
        cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),"new GPU deferred result real worker completes");
    cache.DrainAsyncMeshResults(world,registry,8);
    Expect(cache.GetPendingGpuAppliesCount()==2,"old and new GPU pending owners coexist before retirement");
    std::vector<int> held;
    // New FirstMesh is at queue front: kick it, then retire the old tail.
    ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache,world,registry);
    const bool pending=cache.GetPendingGpuAppliesCount()==1;
    const bool preserved=cache.GetCaptureStore().TryGet(world,root,newer.sourceRevision).has_value() &&
        !cache.IsChunkMeshDirty(root) && !cache.IsRemeshAfterApplyPending(root) &&
        (pending ? cache.GetInflightSourceRevision(root)==newer.sourceRevision &&
                       cache.IsGpuExtractInFlight(root) && cache.IsPendingGpuApply(root)
                 : cache.IsPublishedMeshCurrentForInputs(root,world));
    if(!preserved) std::cout<<"new_gpu_boundary pending="<<cache.GetPendingGpuAppliesCount()
        <<" active="<<cache.GetInflightSourceRevision(root)<<" expected="<<newer.sourceRevision
        <<" capture="<<cache.GetCaptureStore().TryGet(world,root,newer.sourceRevision).has_value()
        <<" dirty="<<cache.IsChunkMeshDirty(root)<<" raa="<<cache.IsRemeshAfterApplyPending(root)
        <<" extract="<<cache.IsGpuExtractInFlight(root)<<" indexed="<<cache.IsPendingGpuApply(root)<<'\n';
    ChunkMeshCaptureAdmissionTestAccess::ReleaseHeldPackedSlots(cache,held);
    for(int n=0;n<8 && cache.GetPendingGpuAppliesCount();++n) {
      ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache,world,registry);glFinish();
    }
    const bool current=cache.IsPublishedMeshCurrentForInputs(root,world) &&
        cache.GetGpuMeshPipeline()->HasGpuMesh(root) && cache.GetPublishCount(root)==1 &&
        !cache.HasPendingDirty() && !cache.IsRemeshAfterApplyPending(root);
    std::cout<<"old_gpu_new_gpu phase="<<phase<<" preserved="<<preserved<<" current="<<current<<'\n';
    Expect(preserved && current,"old GPU retirement preserves newer actual GPU owner and publication");
  }
}

static void TestOldGpuNewCpuOwner(cutum::UBlockRegistry &registry,
                                  const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  for (int phase=0; phase<3; ++phase)
    for (const bool edit : {false,true})
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
    world.SetBlock({8,8,8},kStone);
    const glm::ivec3 root(0);
    GatedPhysicalMesher mesher;
    UChunkMeshCache cache;
    auto settings=MakeFrustumMeshSettings();
    settings.AsyncMeshing=true; settings.GpuPackedMeshing=true;
    settings.FrustumCulling=false; settings.Lighting=LightingMode::Flat;
    cache.SetRenderSettings(settings);
    cache.SetMesherBackend(&mesher);
    cache.SetMeshRebuildFocus(root,4,1);
    const auto capture=[&]() {
      UFlatLightingPipeline flat; flat.FillAllLoadedChunks(world);
      const auto *chunk=world.GetChunkManager().GetChunk(root);
      const auto epoch=cache.GetCaptureStore().WorldEpoch();
      const auto revision=cache.GetChunkMeshRevision(root);
      auto &d=cache.GetRenderDemandStore();
      d.NoteDemand(root,revision,chunk->GetLightFieldRevision(),0,1.0,epoch,chunk->GetIncarnation());
      d.NoteLightCalculationSettled(root,epoch,chunk->GetIncarnation(),
          chunk->GetLightFieldRevision(),chunk->GetContentRevision());
      return ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache,world,root);
    };
    cache.MarkDirtyPriority(root);
    ChunkMeshCaptureAdmissionTestAccess::QueuePacked(cache,capture(),registry);
    if(phase>0) {
      ChunkMeshCaptureAdmissionTestAccess::KickOnly(cache,registry); glFinish();
      if(phase==2) Expect(ChunkMeshCaptureAdmissionTestAccess::AdvancePackedToKicked(cache,registry),
          "mixed owner fixture reaches actual Kicked phase");
    }
    world.SetBlock({12,8,8},kStone);
    cache.InvalidateInFlightMeshBuild(root);
    auto newer=capture();
    Expect(cache.GetCaptureStore().TryCommit(root,newer.sourceRevision,
        newer.sourceWorldEpoch,newer,nullptr),"mixed owner retains current capture");
    Expect(ChunkMeshCaptureAdmissionTestAccess::AdmitCpuThenAdvanceReceiptFrame(
        cache,newer,registry) && mesher.WaitEntered(),"actual newer CPU owner is held at barrier");
    ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache,world,registry);
    const bool preserved=cache.HasCpuMeshBuildInFlight(root) &&
        cache.GetInflightSourceRevision(root)==newer.sourceRevision &&
        cache.GetCaptureStore().TryGet(world,root,newer.sourceRevision).has_value() &&
        !cache.IsChunkMeshDirty(root) && !cache.IsRemeshAfterApplyPending(root) &&
        cache.GetPendingGpuAppliesCount()==0;
    if(edit) {world.SetBlock({8,8,12},kStone); cache.MarkDirty(root);}
    const bool genuine=!edit || cache.IsRemeshAfterApplyPending(root);
    mesher.Release();
    Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)) && !mesher.timed_out,
           "new CPU worker releases explicit barrier");
    cache.DrainAsyncMeshResults(world,registry,8);
    if(edit) {
      auto latest=capture();
      Expect(cache.GetCaptureStore().TryCommit(root,latest.sourceRevision,
          latest.sourceWorldEpoch,latest,nullptr),"real follow-up recaptures input");
      const auto tick=cache.RebuildDirtyChunksWithStats(world,registry,0,8,false,0);
      Expect(tick.Scheduled==1 && cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
          "genuine follow-up actual CPU worker completes");
      cache.DrainAsyncMeshResults(world,registry,8);
    }
    const bool current=cache.IsPublishedMeshCurrentForInputs(root,world) &&
        !cache.HasPendingDirty() && !cache.IsRemeshAfterApplyPending(root) &&
        mesher.builds==(edit?2u:1u);
    std::cout<<"old_gpu_new_cpu phase="<<phase<<" edit="<<edit<<" preserved="<<preserved
             <<" genuine="<<genuine<<" current="<<current<<'\n';
    Expect(preserved && genuine && current,"old GPU retirement preserves newer CPU and genuine follow-up");
  }
}
#endif

static bool TestRetryReceiptAndRealRaa(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  bool all = true;
  for (const bool main_tick : {false, true})
    for (const bool genuine_edit : {false, true})
    {
      UBlockWorld world;
      world.SetFluidDefinitions(definitions);
      world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
      world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      world.SetBlock({8, 8, 8}, kStone);
      const glm::ivec3 root(0);
      GatedPhysicalMesher rejected_mesher, current_mesher;
      rejected_mesher.Release();
      UChunkMeshCache cache;
      auto settings = MakeFrustumMeshSettings();
      settings.AsyncMeshing = true;
      settings.FrustumCulling = false;
      settings.Lighting = LightingMode::Flat;
      cache.SetRenderSettings(settings);
      cache.SetRepairGuardWorld(&world);
      cache.SetMesherBackend(&rejected_mesher);
      cache.SetMeshRebuildFocus(root, 4, 1);
      const auto capture = [&]() {
        UFlatLightingPipeline flat;
        flat.FillAllLoadedChunks(world);
        const auto *chunk = world.GetChunkManager().GetChunk(root);
        const auto revision = cache.GetChunkMeshRevision(root);
        const auto epoch = cache.GetCaptureStore().WorldEpoch();
        cache.GetRenderDemandStore().NoteDemand(root, revision, chunk->GetLightFieldRevision(),
            0, 1.0, epoch, chunk->GetIncarnation());
        cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
            chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
        return ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, root);
      };
      cache.MarkDirtyPriority(root);
      auto input = capture();
      Expect(cache.GetCaptureStore().TryCommit(root, input.sourceRevision,
                 cache.GetCaptureStore().WorldEpoch(), input, nullptr), "retry fixture commits valid captured input");
      UPipelineAdmission::Get().SetResultCap(0);
      const auto tick = cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
      Expect(tick.Scheduled == 1 && cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
             "old actual CPU job creates result-rejection receipt");
      UPipelineAdmission::Get().SetResultCap(128 * 1024 * 1024);
      Expect(cache.GetMeshCompletedSize() == 0 && !cache.HasCpuMeshBuildInFlight(root),
             "failed old result has no executable CPU owner before reissue");
      world.SetBlock({12, 8, 8}, kStone);
      cache.InvalidateInFlightMeshBuild(root);
      cache.SetMesherBackend(&current_mesher);
      Expect(ChunkMeshCaptureAdmissionTestAccess::AdmitCpuThenAdvanceReceiptFrame(
                 cache, capture(), registry) && current_mesher.WaitEntered(),
             "newer actual CPU job owns target before old receipt consumption");
      const auto current_request = cache.GetInflightSourceRevision(root);
      if (main_tick) cache.RebuildDirtyChunksWithStats(world, registry, 0, 0, false, 0);
      else cache.DrainAsyncMeshResults(world, registry, 0);
      const bool coalesced = cache.HasCpuMeshBuildInFlight(root) &&
          cache.GetInflightSourceRevision(root) == current_request &&
          !cache.IsChunkMeshDirty(root) && !cache.IsRemeshAfterApplyPending(root);
      if (genuine_edit)
      {
        world.SetBlock({8, 8, 12}, kStone);
        for (int duplicate = 0; duplicate < 6; ++duplicate) cache.MarkDirty(root);
      }
      const bool real_debt = !genuine_edit || cache.IsRemeshAfterApplyPending(root);
      current_mesher.Release();
      Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)) && !current_mesher.timed_out,
             "current worker exits behind explicit mutation barrier");
      cache.DrainAsyncMeshResults(world, registry, 8);
      bool current = false;
      if (genuine_edit)
      {
        const bool returned = cache.IsChunkMeshDirty(root);
        auto latest = capture();
        Expect(cache.GetCaptureStore().TryCommit(root, latest.sourceRevision,
                   cache.GetCaptureStore().WorldEpoch(), latest, nullptr),
               "real RAA follow-up captures latest geometry");
        const auto retry = cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
        Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)), "real RAA current follow-up computes");
        cache.DrainAsyncMeshResults(world, registry, 8);
        current = returned && retry.Scheduled == 1 && current_mesher.builds == 2;
      }
      else current = current_mesher.builds == 1;
      current = current && cache.IsPublishedMeshCurrentForInputs(root, world) &&
          !cache.HasPendingDirty() && !cache.IsRemeshAfterApplyPending(root);
      std::cout << "retry_raa main=" << main_tick << " edit=" << genuine_edit
                << " coalesced=" << coalesced << " real_debt=" << real_debt
                << " current=" << current << " builds=" << current_mesher.builds << '\n';
      all = all && coalesced && real_debt && current;
    }
  return all;
}

static bool TestMeshAdmissionOwnerLifetime(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  auto &credits = UPipelineAdmission::Get();
  const auto reset = [&]() {
    credits.SetWorkSlotCap(8);
    credits.SetSnapshotCap(96 * 1024 * 1024);
    credits.SetResultCap(128 * 1024 * 1024);
  };
  reset();
  bool all = true;
  for (int mode = 0; mode < 4; ++mode)
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    const glm::ivec3 target(2, 0, 0), other(3, 0, 0);
    for (auto coord : {target, other}) world.SetBlock(coord * CHUNK_SIZE + glm::ivec3(8), kStone);
    GatedPhysicalMesher mesher;
    if (mode != 2) mesher.Release();
    UAsyncMeshBuilder builder(1);
    builder.SetMesher(&mesher);
    builder.SetCompletedCapacity(4);
    MeshBuilderAdmissionTestAccess::LimitPool(builder);
    const auto submit = [&](glm::ivec3 coord, uint64_t revision) {
      return builder.EnqueueDetailed(ChunkMeshSnapshot::Capture(world, coord, revision,
          nullptr, nullptr, MeshLightInputs::PhysicalField), registry);
    };
    if (mode == 2)
    {
      Expect(submit(target, 930) == MeshEnqueueResult::Accepted && mesher.WaitEntered() &&
                 submit(other, 932) == MeshEnqueueResult::Accepted,
             "worker refusal fixture has exact running owner and full queued slot");
    }
    const auto snapshots = credits.SnapshotPendingBytes();
    const auto slots = credits.WorkSlotsInUse();
    const auto results = credits.ResultPendingBytes();
    if (mode == 0) credits.SetWorkSlotCap(0);
    if (mode == 1) credits.SetSnapshotCap(0);
    if (mode == 3) credits.SetResultCap(0);
    const auto outcome = submit(target, 931);
    const auto expected = mode == 0 ? MeshEnqueueResult::WorkSlotRejected :
        mode == 1 ? MeshEnqueueResult::SnapshotBudgetRejected :
        mode == 2 ? MeshEnqueueResult::WorkerPoolRejected : MeshEnqueueResult::Accepted;
    bool good = outcome == expected;
    if (mode != 3)
      good = good && credits.SnapshotPendingBytes() == snapshots &&
          credits.WorkSlotsInUse() == slots && credits.ResultPendingBytes() == results &&
          builder.IsInFlight(target) == (mode == 2);
    mesher.Release();
    Expect(builder.WaitIdleFor(std::chrono::seconds(5)) && !mesher.timed_out,
           "admission fixture workers retire behind bounded barrier");
    reset();
    auto completed = builder.DrainCompleted(4);
    if (mode == 2)
      good = good && std::any_of(completed.begin(), completed.end(), [](const auto &r) {
        return r.sourceRevision == 930;
      });
    const auto retry = builder.TakeOverflowCoords();
    good = good && (mode == 3 ? retry.size() == 1 && retry[0] == target : retry.empty());
    completed.clear();
    good = good && credits.SnapshotPendingBytes() == 0 && credits.WorkSlotsInUse() == 0 &&
        credits.ResultPendingBytes() == 0;
    world.SetBlock(target * CHUNK_SIZE + glm::ivec3(12, 8, 8), kStone);
    Expect(submit(target, 933) == MeshEnqueueResult::Accepted &&
               builder.WaitIdleFor(std::chrono::seconds(5)),
           "post-refusal current demand reaches actual worker");
    auto current = builder.DrainCompleted(1);
    std::size_t indices = 0;
    if (current.size() == 1)
      for (const auto &batch : current[0].batches) indices += batch.indices.size();
    good = good && current.size() == 1 && current[0].sourceRevision == 933 && indices > 36;
    std::cout << "mesh_admission_owner mode=" << mode << " outcome=" << static_cast<int>(outcome)
              << " good=" << good << '\n';
    all = all && good;
  }
  reset();
  return all;
}

static bool TestCacheMeshAdmissionDebt(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  auto &credits = UPipelineAdmission::Get();
  bool all = true;
  for (int mode = 0; mode < 3; ++mode)
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    const glm::ivec3 root(0);
    world.SetBlock({8, 8, 8}, kStone);
    GatedPhysicalMesher mesher;
    mesher.Release();
    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.AsyncMeshing = true;
    settings.FrustumCulling = false;
    settings.Lighting = LightingMode::Flat;
    cache.SetRenderSettings(settings);
    cache.SetMesherBackend(&mesher);
    cache.SetMeshRebuildFocus(root, 4, 1);
    UFlatLightingPipeline flat;
    flat.FillAllLoadedChunks(world);
    cache.MarkDirtyPriority(root);
    const auto revision = cache.GetChunkMeshRevision(root);
    const auto *chunk = world.GetChunkManager().GetChunk(root);
    const auto epoch = cache.GetCaptureStore().WorldEpoch();
    cache.GetRenderDemandStore().NoteDemand(root, revision, chunk->GetLightFieldRevision(),
        0, 1.0, epoch, chunk->GetIncarnation());
    cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
        chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
    Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
               cache, world, root, revision).has_value(), "cache refusal fixture starts with valid capture");
    if (mode == 0) credits.SetWorkSlotCap(0);
    if (mode == 1) credits.SetSnapshotCap(0);
    if (mode == 2) credits.SetResultCap(0);
    const auto tick = cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
    Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)), "cache refusal worker phase finishes");
    credits.SetWorkSlotCap(8);
    credits.SetSnapshotCap(96 * 1024 * 1024);
    credits.SetResultCap(128 * 1024 * 1024);
    cache.DrainAsyncMeshResults(world, registry, 0);
    const bool retained = cache.IsChunkMeshDirty(root) && !cache.HasCpuMeshBuildInFlight(root) &&
        !cache.HasInflightMeshBuild(root) && cache.GetChunkMeshRevision(root) == revision;
    Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
               cache, world, root, cache.GetChunkMeshRevision(root)).has_value(),
           "retained admission debt can recapture current input without fresh MarkDirty");
    const auto retry = cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
    Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)), "cache refusal successor worker finishes");
    cache.DrainAsyncMeshResults(world, registry, 8);
    const bool current = retry.Scheduled == 1 && cache.IsPublishedMeshCurrentForInputs(root, world);
    std::cout << "cache_admission_debt mode=" << mode << " scheduled=" << tick.Scheduled
              << " retained=" << retained << " successor=" << current << '\n';
    all = all && retained && current;
  }
  return all;
}

static bool TestCaptureStoreDemandLifetime(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  auto &credits = UPipelineAdmission::Get();
  bool all = true;
  for (int mode = 0; mode < 4; ++mode)
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    const glm::ivec3 root(0);
    world.SetBlock({8, 8, 8}, kStone);
    GatedPhysicalMesher mesher;
    mesher.Release();
    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.AsyncMeshing = true;
    settings.FrustumCulling = false;
    settings.Lighting = LightingMode::Flat;
    cache.SetRenderSettings(settings);
    cache.SetMesherBackend(&mesher);
    cache.SetMeshRebuildFocus(root, 4, 1);
    UFlatLightingPipeline flat;
    flat.FillAllLoadedChunks(world);
    cache.MarkDirtyPriority(root);
    const auto revision = cache.GetChunkMeshRevision(root);
    const auto *chunk = world.GetChunkManager().GetChunk(root);
    const auto epoch = cache.GetCaptureStore().WorldEpoch();
    cache.GetRenderDemandStore().NoteDemand(root, revision, chunk->GetLightFieldRevision(),
        0, 1.0, epoch, chunk->GetIncarnation());
    cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
        chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
    auto &store = cache.GetCaptureStore();
    const auto baseline = credits.SnapshotPendingBytes();
    bool boundary = false;
    if (mode == 0)
    {
      credits.SetSnapshotCap(baseline);
      boundary = !ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
          cache, world, root, revision) && store.Size() == 0;
    }
    else
    {
      Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
          cache, world, root, revision).has_value(), "capture lifecycle initial capture");
      if (mode == 1)
      {
        const auto evictions = store.CapacityEvictions();
        for (size_t n = 0; n < UMeshCaptureStore::kMaxRetainedEntries; ++n)
          Expect(store.CaptureAndStore(world, {static_cast<int>(n + 4), 0, 0}, 1).has_value(),
                 "capacity fixture uses actual main-thread captures");
        boundary = store.Size() == UMeshCaptureStore::kMaxRetainedEntries &&
            store.CapacityEvictions() == evictions + 1 && !store.TryGet(world, root, revision);
      }
      else if (mode == 2)
      {
        const auto evictions = store.PressureEvictions();
        credits.SetSnapshotCap(baseline + kEstimatedChunkSnapshotBytes);
        boundary = store.CaptureAndStore(world, {4, 0, 0}, 1).has_value() &&
            store.PressureEvictions() == evictions + 1 && !store.TryGet(world, root, revision);
      }
      else
      {
        const auto evictions = store.StaleEntryEvictions();
        world.SetBlock({10, 8, 8}, kStone);
        flat.FillAllLoadedChunks(world);
        cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
            chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
        boundary = !store.TryGet(world, root, revision) &&
            store.StaleEntryEvictions() == evictions + 1;
      }
    }
    credits.SetSnapshotCap(96 * 1024 * 1024);
    const bool retained = cache.IsChunkMeshDirty(root) &&
        cache.GetChunkMeshRevision(root) == revision && !cache.HasInflightMeshBuild(root);
    Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
        cache, world, root, revision).has_value(), "capture lifecycle recaptures without new dirty ingress");
    const auto retry = cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
    Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)), "capture lifecycle successor finishes");
    cache.DrainAsyncMeshResults(world, registry, 8);
    const bool current = retry.Scheduled == 1 && cache.IsPublishedMeshCurrentForInputs(root, world);
    std::cout << "capture_store_lifetime mode=" << mode << " boundary=" << boundary
              << " retained=" << retained << " current=" << current << '\n';
    all = all && boundary && retained && current;
  }
  return all;
}

static void TestOutsideRadiusCpuMeshLifetime(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  bool boundaries = true;
  // no predecessor; retained predecessor; explicit lease; stale-light lease;
  // stale-light debt beyond its bounded protection ring.
  for (int mode = 0; mode < 5; ++mode)
    for (const bool completed : {false, true})
    {
      const bool predecessor = mode == 1 || mode == 3 || mode == 4;
      const bool protected_job = mode == 2 || mode == 3;
      const glm::ivec3 root(0), focus(mode == 4 ? 32 : 2, 0, 0);
      UBlockWorld world;
      world.SetFluidDefinitions(definitions);
      world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
      world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      world.SetBlock({8, 8, 8}, kStone);
      GatedPhysicalMesher mesher;
      if (completed) mesher.Release();
      UChunkMeshCache cache;
      auto settings = MakeFrustumMeshSettings();
      settings.AsyncMeshing = true;
      settings.FrustumCulling = false;
      settings.Lighting = LightingMode::Flat;
      cache.SetRenderSettings(settings);
      cache.SetMeshRebuildFocus(root, 4, 1);
      UFlatLightingPipeline flat;
      if (predecessor)
      {
        flat.FillAllLoadedChunks(world);
        cache.RebuildChunkImmediate(world, registry, root);
        Expect(cache.HasDrawableGreedyMesh(root), "outside-radius fixture retains actual predecessor");
        world.SetBlock({12, 8, 8}, kStone);
      }
      const auto initial_publications = cache.GetPublishCount(root);
      cache.SetMesherBackend(&mesher);
      const auto schedule = [&]() {
        flat.FillAllLoadedChunks(world);
        const auto *chunk = world.GetChunkManager().GetChunk(root);
        const auto revision = cache.GetChunkMeshRevision(root);
        const auto epoch = cache.GetCaptureStore().WorldEpoch();
        cache.GetRenderDemandStore().NoteDemand(root, revision, chunk->GetLightFieldRevision(),
            0, 1.0, epoch, chunk->GetIncarnation());
        cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
            chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
        Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
                   cache, world, root, revision).has_value(), "outside-radius captures exact target");
        const auto tick = cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
        Expect(tick.Scheduled == 1 && cache.HasCpuMeshBuildInFlight(root),
               "outside-radius fixture owns exact actual CPU job");
      };
      cache.MarkDirtyPriority(root);
      schedule();
      Expect(mesher.WaitEntered(), "outside-radius actual worker reaches barrier");
      if (completed)
        Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
               "outside-radius completed result awaits consumption");
      if (mode >= 3)
      {
        cache.QueueStaleLightRemesh(root, "outside-radius-lifecycle-test");
        Expect(cache.HasPendingStaleLightRemesh(root), "actual stale-light repair debt acquires lease candidate");
      }
      cache.SetMeshRebuildFocus(focus, 4, 1);
      cache.CancelInFlightOutsideHorizontalRadius(focus, 0, mode == 2 ? 2 : -1);
      const bool cpu_owner = cache.HasCpuMeshBuildInFlight(root);
      const bool cache_owner = cache.HasInflightMeshBuild(root);
      if (!protected_job)
        cache.RebuildDirtyChunksWithStats(world, registry, 0, 0, false, 0);
      mesher.Release();
      Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)) && !mesher.timed_out,
             "outside-radius worker retires after bounded barrier");
      cache.DrainAsyncMeshResults(world, registry, 8);
      cache.DrainAsyncMeshResults(world, registry, 8);
      const bool terminal = protected_job
          ? cpu_owner && cache_owner && cache.IsPublishedMeshCurrentForInputs(root, world) &&
                cache.GetPublishCount(root) == initial_publications + 1 && mesher.builds == 1
          : !cpu_owner && !cache_owner && !cache.IsChunkMeshDirty(root) &&
                cache.HasDrawableGreedyMesh(root) == predecessor &&
                cache.GetPublishCount(root) == initial_publications;
      boundaries = boundaries && terminal;
      std::cout << "outside_cpu_case mode=" << mode << " completed=" << completed
                << " protected=" << protected_job << " cpu_owner=" << cpu_owner
                << " cache_owner=" << cache_owner << " dirty=" << cache.IsChunkMeshDirty(root)
                << " terminal=" << terminal << '\n';
      if (!protected_job)
      {
        cache.SetMeshRebuildFocus(root, 4, 1);
        cache.MarkDirtyPriority(root);
        schedule();
        Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
               "returning demand after radius cancellation computes current geometry");
        cache.DrainAsyncMeshResults(world, registry, 8);
      }
      Expect(cache.IsPublishedMeshCurrentForInputs(root, world),
             "protected original job or returning demand publishes current inputs");
    }
  Expect(boundaries, "outside-radius cancellation retires only unprotected CPU work without resurrection");
}

static void TestCancelledMeshRetryReceipts(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  bool outcomes = true;
  for (int mode = 0; mode < 4; ++mode)
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    const glm::ivec3 victim(2, 0, 0), newer(3, 0, 0);
    for (const auto coord : {victim, newer})
      world.SetBlock(coord * CHUNK_SIZE + glm::ivec3(8), kStone);
    GatedPhysicalMesher mesher;
    mesher.Release();
    UAsyncMeshBuilder builder(2);
    builder.SetMesher(&mesher);
    builder.SetCompletedCapacity(1);
    const auto submit = [&](glm::ivec3 coord, uint64_t revision) {
      return builder.Enqueue(ChunkMeshSnapshot::Capture(world, coord, revision,
          nullptr, nullptr, MeshLightInputs::PhysicalField), registry);
    };
    Expect(submit(victim, 921) && builder.WaitIdleFor(std::chrono::seconds(5)) &&
               submit(newer, 922) && builder.WaitIdleFor(std::chrono::seconds(5)) &&
               builder.GetCompletedDiscardedOverflow() == 1,
           "cancel receipt fixture produces a real overflow retry before cancellation");
    if (mode == 3)
    {
      builder.ForgetInflight(victim);
      builder.ForgetInflight(newer);
    }
    else if (mode == 0) builder.CancelPending();
    else builder.CancelPendingOutsideHorizontalRadius(mode == 2 ? victim : glm::ivec3(0), 0);
    const auto retry = builder.TakeOverflowCoords();
    const bool expected_retry = mode == 2;
    const bool good = expected_retry ? retry.size() == 1 && retry[0] == victim : retry.empty();
    outcomes = outcomes && good;
    std::cout << "cancel_mesh_receipt mode=" << mode << " retry_count=" << retry.size()
              << " expected=" << expected_retry << '\n';
    Expect(builder.DrainCompleted(4).empty() && !builder.IsInFlight(newer),
           "cancel receipts retire outside completion owners");
    Expect(submit(victim, 923) && builder.WaitIdleFor(std::chrono::seconds(5)),
           "protected retry or fresh post-cancel demand reaches actual worker");
    auto result = builder.DrainCompleted(1);
    Expect(result.size() == 1 && result[0].coord == victim &&
               result[0].sourceRevision == 923 && !result[0].batches.empty(),
           "post-cancel receipt policy permits actual current geometry completion");
  }
  Expect(outcomes, "cancellation retires abandoned retry receipts and preserves protected receipts");
}

static void TestForgottenMeshJobLifetime(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  bool no_retired_retry = true;
  for (int scenario = 0; scenario < 10; ++scenario)
  {
    const bool queued_target = scenario == 0 || scenario >= 8;
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    const glm::ivec3 target(2, 0, 0), blocker(3, 0, 0);
    for (const auto coord : {target, blocker})
      world.SetBlock(coord * CHUNK_SIZE + glm::ivec3(8), kStone);
    GatedPhysicalMesher mesher;
    if (scenario == 2) mesher.Release();
    UAsyncMeshBuilder builder(queued_target ? 1 : 2);
    builder.SetMesher(&mesher);
    builder.SetCompletedCapacity(4);
    const auto submit = [&](glm::ivec3 coord, uint64_t revision) {
      return builder.Enqueue(ChunkMeshSnapshot::Capture(world, coord, revision,
          nullptr, nullptr, MeshLightInputs::PhysicalField), registry);
    };
    if (queued_target)
      Expect(submit(blocker, 910) && mesher.WaitEntered(),
             "forget queued fixture holds unrelated worker");
    Expect(submit(target, 911), "forget fixture accepts target job");
    if (!queued_target)
      Expect(mesher.WaitEntered(), "forget fixture enters target worker");
    if (scenario == 2)
      Expect(builder.WaitIdleFor(std::chrono::seconds(5)),
             "forget completed fixture reaches completed queue");
    if (scenario >= 4)
    {
      if (scenario == 4 || scenario == 6 || scenario == 8) builder.CancelPending();
      else builder.CancelPendingOutsideHorizontalRadius({0, 0, 0}, 0);
    }
    if (scenario == 3 || scenario == 4 || scenario == 5)
    {
      world.SetBlock(target * CHUNK_SIZE + glm::ivec3(12, 8, 8), kStone);
      Expect(submit(target, 912), "post-retirement successor accepted");
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (builder.GetCompletedSize() != 1 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
      auto current = builder.DrainCompleted(1);
      Expect(current.size() == 1 && current[0].sourceRevision == 912 &&
                 !builder.IsInFlight(target),
             "newer same-key job fully retires before old worker release");
    }
    else if (scenario < 4)
      builder.ForgetInflight(target);
    mesher.Release();
    Expect(builder.WaitIdleFor(std::chrono::seconds(5)) && !mesher.timed_out,
           "forgotten and superseded workers retire behind barrier");
    auto remaining = builder.DrainCompleted(4);
    Expect(std::none_of(remaining.begin(), remaining.end(), [&](const auto &r) {
             return r.coord == target;
           }) && !builder.IsInFlight(target),
           "retired old target cannot return accepted geometry");
    const auto debt = builder.TakeDiscardedCoords();
    const bool clean = debt.empty() && builder.TakeOverflowCoords().empty();
    no_retired_retry = no_retired_retry && clean;
    std::cout << "forgotten_mesh_case=" << scenario << " retry_count=" << debt.size() << '\n';
    Expect(submit(target, 913) && builder.WaitIdleFor(std::chrono::seconds(5)),
           "fresh demand after retirement reaches actual worker");
    auto current = builder.DrainCompleted(1);
    Expect(current.size() == 1 && current[0].coord == target &&
               current[0].sourceRevision == 913 && !current[0].batches.empty(),
           "fresh demand after retirement completes actual geometry");
    if (scenario >= 8)
      Expect(mesher.builds == 2, "cancelled queued target never enters mesher before fresh demand");
  }
  Expect(no_retired_retry, "forgotten cancelled or fully superseded jobs never resurrect executable debt");
}

static void TestMeshSameKeySupersession(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  for (const int scenario : {0, 1, 2, 3})
  {
    const bool running_old = scenario == 1;
    const bool reduce_capacity = scenario == 2;
    const bool push_overflow = scenario == 3;
    std::cout << "mesh_same_key_case push_overflow=" << push_overflow << " capacity_reduction=" << reduce_capacity << " running_old=" << running_old << " START\n";
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    const glm::ivec3 coord(2, 0, 0), origin = coord * CHUNK_SIZE;
    world.SetBlock(origin + glm::ivec3(8), kStone);
    GatedPhysicalMesher mesher;
    if (!running_old) mesher.Release();
    UAsyncMeshBuilder builder(2);
    builder.SetMesher(&mesher);
    builder.SetCompletedCapacity(push_overflow ? 1 : 2);
    const auto submit = [&](uint64_t revision) {
      return builder.Enqueue(ChunkMeshSnapshot::Capture(world, coord, revision,
          nullptr, nullptr, MeshLightInputs::PhysicalField), registry);
    };
    Expect(submit(901) && mesher.WaitEntered(), "same-key predecessor reaches worker barrier");
    if (running_old)
      builder.CancelPendingOutsideHorizontalRadius({0, 0, 0}, 0);
    else
      Expect(builder.WaitIdleFor(std::chrono::seconds(5)), "same-key old completion ready");
    world.SetBlock(origin + glm::ivec3(12, 8, 8), kStone);
    Expect(submit(902), "same-key newer input request accepted");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto ready_count = (running_old || push_overflow) ? 1u : 2u;
    while (builder.GetCompletedSize() < ready_count && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    Expect(builder.GetCompletedSize() == ready_count && builder.IsInFlight(coord),
           "newer same-key completion owns the coordinate before old retirement");
    mesher.Release();
    Expect(builder.WaitIdleFor(std::chrono::seconds(5)) && !mesher.timed_out,
           "same-key workers retire after barrier release");
    if (reduce_capacity)
      builder.SetCompletedCapacity(1);
    else if (!running_old && !push_overflow)
      Expect(builder.DrainCompleted(1).empty(), "superseded old completion rejected");
    Expect(builder.IsInFlight(coord) && builder.GetCompletedDiscardedOverflow() == ((reduce_capacity || push_overflow) ? 1u : 0u),
           "old retirement preserves newer same-key owner and result");
    Expect(builder.TakeDiscardedCoords().empty(),
           "superseded same-key result does not return duplicate executable debt");
    Expect(builder.TakeOverflowCoords().empty(),
           "superseded same-key capacity victim does not return duplicate executable debt");
    auto results = builder.DrainCompleted(1);
    std::size_t indices = 0;
    if (results.size() == 1)
      for (const auto &batch : results[0].batches) indices += batch.indices.size();
    Expect(results.size() == 1 && results[0].coord == coord &&
               results[0].sourceRevision == 902 && indices > 36 && !builder.IsInFlight(coord),
           "same-key current result preserves newer two-cube inputs and retires owner once");
    Expect(builder.DrainCompleted(1).empty() && builder.TakeDiscardedCoords().empty(),
           "same-key terminal result and retry debt drain exactly once");
    std::cout << "mesh_same_key_supersession push_overflow=" << push_overflow << " capacity_reduction=" << reduce_capacity << " running_old=" << running_old << " PASS\n";
  }
  // A real victim without a replacement must still return its retry debt.
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  const glm::ivec3 victim(2, 0, 0), kept(3, 0, 0);
  world.SetBlock(victim * CHUNK_SIZE + glm::ivec3(8), kStone);
  world.SetBlock(kept * CHUNK_SIZE + glm::ivec3(8), kStone);
  GatedPhysicalMesher mesher;
  mesher.Release();
  UAsyncMeshBuilder builder(2);
  builder.SetMesher(&mesher);
  builder.SetCompletedCapacity(2);
  const auto submit = [&](glm::ivec3 coord, uint64_t revision) {
    return builder.Enqueue(ChunkMeshSnapshot::Capture(world, coord, revision,
        nullptr, nullptr, MeshLightInputs::PhysicalField), registry);
  };
  Expect(submit(victim, 903) && builder.WaitIdleFor(std::chrono::seconds(5)) &&
             submit(kept, 904) && builder.WaitIdleFor(std::chrono::seconds(5)),
         "distinct capacity victims complete in fixed order");
  builder.SetCompletedCapacity(1);
  const auto retry = builder.TakeOverflowCoords();
  Expect(retry.size() == 1 && retry[0] == victim &&
             !builder.IsInFlight(victim) && builder.IsInFlight(kept),
         "capacity shrink returns exact unowned victim debt and preserves kept owner");
  auto result = builder.DrainCompleted(1);
  Expect(result.size() == 1 && result[0].coord == kept &&
             result[0].sourceRevision == 904,
         "capacity shrink keeps actual newest distinct completion");
  result.clear();
  Expect(submit(retry[0], 905) && builder.WaitIdleFor(std::chrono::seconds(5)),
         "capacity victim retry reaches actual worker");
  result = builder.DrainCompleted(1);
  Expect(result.size() == 1 && result[0].coord == victim &&
             result[0].sourceRevision == 905 && !result[0].batches.empty() &&
             builder.TakeOverflowCoords().empty() && !builder.IsInFlight(victim),
         "capacity victim retry completes real current geometry exactly once");
  std::cout << "mesh_capacity_distinct_victim_retry PASS\n";
  // Repeated actual capacity loss must retain one coordinate obligation,
  // rather than one notification per historical job. Do not drain retries
  // between rounds; each successor is a new real physical mesh submission.
  for(int round=0;round<8;++round) {
    Expect(submit(victim,1000+round*2) && builder.WaitIdleFor(std::chrono::seconds(5)) &&
               submit(kept,1001+round*2) && builder.WaitIdleFor(std::chrono::seconds(5)),
           "repeated overflow cohort completes real jobs");
    auto newest=builder.DrainCompleted(1);
    Expect(newest.size()==1 && newest[0].coord==kept,
           "repeated overflow preserves current distinct owner");
  }
  const auto coalesced=builder.TakeOverflowCoords();
  Expect(coalesced.size()==1 && coalesced[0]==victim && builder.TakeOverflowCoords().empty(),
         "eight real overflow victims coalesce into one transferred retry obligation");
  Expect(submit(victim,1100) && builder.WaitIdleFor(std::chrono::seconds(5)) &&
             submit(kept,1101) && builder.WaitIdleFor(std::chrono::seconds(5)),
         "forgotten overflow has actual pending retry");
  builder.ForgetInflight(victim);
  Expect(builder.TakeOverflowCoords().empty() && builder.IsInFlight(kept),
         "keyed retry retirement preserves independent completed owner");
  builder.DrainCompleted(1).clear();
  Expect(submit(victim,1102) && builder.WaitIdleFor(std::chrono::seconds(5)),
         "coalesced retry successor reaches physical worker");
  auto successor=builder.DrainCompleted(1);
  Expect(successor.size()==1 && successor[0].sourceRevision==1102 &&
             !successor[0].batches.empty(),"coalesced retry successor publishes current geometry");
  std::cout << "mesh_retry_history_coalescing rounds=8 obligations=" << coalesced.size() << " PASS\n";
  {
    UAsyncMeshBuilder limited(1);
    limited.SetMesher(&mesher);
    const auto empty_retry=limited.GetRetryStorageSnapshot();
    auto &credits=UPipelineAdmission::Get();
    successor.clear();
    credits.SetResultCap(0);
    const auto enqueue=[&](glm::ivec3 coord,uint64_t revision) {
      return limited.EnqueueDetailed(ChunkMeshSnapshot::Capture(world,coord,revision,
          nullptr,nullptr,MeshLightInputs::PhysicalField),registry);
    };
    bool all_admitted=true;
    for(size_t n=0;n<UAsyncMeshBuilder::kMaxOwnedCoordinates;++n) {
      const auto outcome=enqueue({2000+static_cast<int>(n),0,0},1200+n);
      all_admitted=all_admitted && outcome==MeshEnqueueResult::Accepted;
      if(outcome!=MeshEnqueueResult::Accepted ||
          !limited.WaitIdleFor(std::chrono::seconds(5))) { all_admitted=false; break; }
    }
    Expect(all_admitted,"actual result refusal fills declared distinct retry capacity");
    const auto full_retry=limited.GetRetryStorageSnapshot();
    Expect(full_retry.overflow_count==UAsyncMeshBuilder::kMaxOwnedCoordinates &&
        full_retry.in_flight_count==0 && full_retry.discarded_count==0 &&
        full_retry.overflow.live_bytes>empty_retry.overflow.live_bytes,
        "retry allocator accounts actual full distinct coordinate set");
    const glm::ivec3 refused(4000,0,0),released(2000,0,0);
    const auto snapshot_before=credits.SnapshotPendingBytes();
    const auto result_before=credits.ResultPendingBytes();
    Expect(enqueue(refused,2300)==MeshEnqueueResult::RetryCapacityRejected &&
        !limited.IsInFlight(refused) && credits.WorkSlotsInUse()==0 &&
        credits.SnapshotPendingBytes()==snapshot_before && credits.ResultPendingBytes()==result_before,
        "retry capacity refuses new key without ghost owner or leaked credits");
    Expect(enqueue(released,2301)==MeshEnqueueResult::Accepted &&
        limited.WaitIdleFor(std::chrono::seconds(5)),"same key remains admissible at retry capacity");
    limited.ForgetInflight(released);
    credits.SetResultCap(128*1024*1024);
    world.SetBlock(refused*CHUNK_SIZE+glm::ivec3(8),kStone);
    Expect(enqueue(refused,2302)==MeshEnqueueResult::Accepted &&
        limited.WaitIdleFor(std::chrono::seconds(5)),"one exact retirement restores admission");
    auto fresh=limited.DrainCompleted(1);
    Expect(fresh.size()==1 && fresh[0].coord==refused && fresh[0].sourceRevision==2302 &&
        !fresh[0].batches.empty(),"post-capacity current physical successor completes");
    const auto retained=limited.TakeOverflowCoords();
    Expect(retained.size()==UAsyncMeshBuilder::kMaxOwnedCoordinates-1 &&
        std::find(retained.begin(),retained.end(),released)==retained.end() &&
        std::find(retained.begin(),retained.end(),refused)==retained.end() &&
        limited.TakeOverflowCoords().empty(),"all other admitted retry obligations transfer exactly once");
    const auto empty_again=limited.GetRetryStorageSnapshot();
    Expect(empty_again.overflow.live_bytes==empty_retry.overflow.live_bytes &&
        empty_again.discarded.live_bytes==empty_retry.discarded.live_bytes,
        "retry transfer releases actual allocator nodes and buckets to baseline");
    std::cout << "mesh_retry_capacity limit=" << UAsyncMeshBuilder::kMaxOwnedCoordinates
        << " retained=" << retained.size() << " current_successor=" << fresh.size() << " PASS\n";
    std::cout << "mesh_retry_allocator empty=" << empty_retry.overflow.live_bytes
        << " full=" << full_retry.overflow.live_bytes
        << " peak=" << full_retry.overflow.peak_bytes
        << " after=" << empty_again.overflow.live_bytes << '\n';
    fresh.clear();
    bool mixed_admitted=true;
    for(size_t n=0;n<UAsyncMeshBuilder::kMaxOwnedCoordinates;++n) {
      credits.SetResultCap(n%2==0 ? 0 : 128*1024*1024);
      const auto outcome=enqueue({5000+static_cast<int>(n),0,0},3000+n);
      if(outcome!=MeshEnqueueResult::Accepted ||
          !limited.WaitIdleFor(std::chrono::seconds(5))) { mixed_admitted=false; break; }
      if(n%2!=0) {
        MeshBuilderAdmissionTestAccess::AdvanceDeliveryEpoch(limited);
        Expect(limited.DrainCompleted(1).empty(),"stale real completion transfers to discarded retry owner");
      }
    }
    credits.SetResultCap(128*1024*1024);
    const auto mixed=limited.GetRetryStorageSnapshot();
    Expect(mixed_admitted && mixed.overflow_count==512 && mixed.discarded_count==512 &&
        mixed.in_flight_count==0 &&
        enqueue({7000,0,0},4100)==MeshEnqueueResult::RetryCapacityRejected,
        "overflow and stale retries share one distinct-key admission boundary");
    limited.ForgetInflight({5001,0,0});
    Expect(enqueue({7000,0,0},4101)==MeshEnqueueResult::Accepted &&
        limited.WaitIdleFor(std::chrono::seconds(5)) && limited.DrainCompleted(1).size()==1,
        "discarded key retirement restores actual admission");
    const auto mixed_overflow=limited.TakeOverflowCoords();
    const auto mixed_discarded=limited.TakeDiscardedCoords();
    const auto mixed_empty=limited.GetRetryStorageSnapshot();
    Expect(mixed_overflow.size()==512 && mixed_discarded.size()==511 &&
        mixed_empty.overflow.live_bytes==empty_retry.overflow.live_bytes &&
        mixed_empty.discarded.live_bytes==empty_retry.discarded.live_bytes,
        "mixed retry obligations transfer once and both allocators release to baseline");
    std::cout << "mesh_retry_mixed_capacity overflow=" << mixed_overflow.size()
        << " discarded=" << mixed_discarded.size() << " PASS\n";
  }
}

static void TestCertifiedGeometrySurvivesInflightLight(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions, bool use_gpu = false)
{
  using namespace cutum;
  const glm::ivec3 root(0);
  for (int scenario = 0; scenario < 14; ++scenario)
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetLightingConfigurationIdentity(
        static_cast<uint64_t>(LightingMode::Full));
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    world.SetBlock({14, 8, 14}, kStone);
    GatedPhysicalMesher mesher;
    SectionRenderLightBindings resources;
    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.AsyncMeshing = true;
    settings.FrustumCulling = false;
    cache.SetRenderSettings(settings);
    cache.SetMesherBackend(&mesher);
    cache.SetMeshRebuildFocus(root, 4, 1);
    cache.SetRepairGuardWorld(&world);
    int preparations = 0;
    bool reject_resource = scenario == 7;
    cache.SetPrepareRenderLightFn(
        [&](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
            const SectionRenderLightSnapshot &light)
            -> std::shared_ptr<SectionRenderLightBinding>
        {
          ++preparations;
          if (reject_resource)
            return {};
          std::shared_ptr<SectionRenderLightBinding> prepared =
              use_gpu ? resources.PrepareForWorld(epoch, geometry, light)
                      : std::make_shared<TestRenderLightBinding>(
                            epoch, geometry, light);
          if (scenario >= 12)
          {
            cache.GetCaptureStore().SetLightInputsPolicy(
                MeshLightInputs::PhysicalField);
            cache.GetCaptureStore().SetLightInputsPolicy(
                MeshLightInputs::Baked);
          }
          return scenario == 13 ? nullptr : prepared;
        });
    const auto calculate = [&](bool settle = true)
    {
      RelightJobSpec spec;
      spec.min_world_y = world.GetVerticalProfile().MinY;
      spec.max_world_y = world.GetVerticalProfile().MaxY;
      const auto result =
          UChunkRelightSnapshot::CaptureSections(world, spec, {root})
              .Compute(registry);
      Expect(result.inputs_complete && result.chunks.size() == 1,
             "inflight-light fixture computes actual current physical field");
      world.GetChunkManager().ReplaceComputedSectionLightVolume(
          root, *result.chunks[0].padded_light, true, true);
      Expect(world.RecordLightCalculation(
                 result, registry.GetDefinitionsCatalogSnapshot()) == 1,
             "inflight-light fixture installs actual calculation certificate");
      const auto *chunk = world.GetChunkManager().GetChunk(root);
      auto &demand = cache.GetRenderDemandStore();
      const auto epoch = cache.GetCaptureStore().WorldEpoch();
      demand.NoteDemand(root, cache.GetChunkMeshRevision(root),
                        chunk->GetLightFieldRevision(), 0, 1.0, epoch,
                        chunk->GetIncarnation());
      if (settle)
        demand.NoteLightCalculationSettled(root, epoch, chunk->GetIncarnation(),
                                           chunk->GetLightFieldRevision(),
                                           chunk->GetContentRevision());
    };
    cache.MarkDirtyPriority(root);
    calculate();
    const auto revision = cache.GetChunkMeshRevision(root);
    const auto old_light =
        world.GetChunkManager().GetChunk(root)->GetLightFieldRevision();
    Expect(
        ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(cache, world,
                                                                root, revision)
                .has_value() &&
            cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0)
                    .Scheduled == 1 &&
            mesher.WaitEntered() && mesher.captured_physical && old_light != 0,
        "actual worker pauses after building light-free geometry while current "
        "field exists");
    if (scenario == 1)
    {
      mesher.Release();
      Expect(
          cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
          "completed result remains queued before independent field changes");
    }
    if (scenario == 9)
      for (int x = -16; x < 32; ++x)
        for (int z = -16; z < 32; ++z)
          world.SetBlock({x, 63, z}, kStone);
    else
      world.SetBlock({16, 8, 16}, kFire);
    if (scenario == 3)
      world.SetBlock({13, 8, 14}, kStone);
    if (scenario == 6)
      world.SetBlock({16, 8, 14}, kStone);
    if (scenario != 2)
      calculate(scenario != 10);
    if (scenario == 4)
      cache.GetCaptureStore().BumpWorldEpoch();
    if (scenario == 5)
      cache.CancelAsyncMeshWork();
    if (scenario == 11)
    {
      cache.GetCaptureStore().SetLightInputsPolicy(
          MeshLightInputs::PhysicalField);
      cache.GetCaptureStore().SetLightInputsPolicy(MeshLightInputs::Baked);
    }
    if (scenario == 8)
    {
      cache.RebuildDirtyChunksWithStats(world, registry, 0, 0, false, 0);
      cache.MarkDirtyPriority(
          root, MeshRevisionBumpReason::PriorityRelitInstallRepair);
    }
    mesher.Release();
    Expect(cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)) &&
               !mesher.timed_out,
           "actual worker resumes and returns the original immutable geometry");
    cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
    if (scenario >= 11)
    {
      Expect(cache.GetPublishCount(root) == 0 && mesher.builds == 1 &&
                 preparations == (scenario >= 12 ? 1 : 0) &&
                 cache.GetChunkMeshRevision(root) == revision,
             "policy generation change before apply or during preparation "
             "rejects original geometry");
      if (scenario == 11)
        Expect(!cache.HasInflightMeshBuild(root) &&
                   cache.IsChunkMeshDirty(root) &&
                   cache.GetGeometryAwaitingLightCount() == 0,
               "policy transition retires obsolete executable owner and "
               "requeues exact target");
      continue;
    }
    if (scenario == 3 || scenario == 4 || scenario == 5 || scenario == 6)
    {
      Expect(cache.GetPublishCount(root) == 0 && preparations == 0 &&
                 mesher.builds == 1,
             "edited cancelled or wrong-epoch geometry cannot use fresh field "
             "as publication proof");
      if (scenario == 3 || scenario == 6)
      {
        Expect(cache.IsChunkMeshDirty(root),
               "inflight geometry mutation preserves an actual rebuild "
               "obligation");
        const auto next_revision = cache.GetChunkMeshRevision(root);
        Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
                   cache, world, root, next_revision)
                       .has_value() &&
                   cache.RebuildDirtyChunksWithStats(world, registry, 0, 1,
                                                     false, 0)
                           .Scheduled == 1 &&
                   cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
               "changed geometry is captured and computed by a replacement "
               "worker");
        cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
        const auto replacement = cache.GetPublishedRenderLightBinding(root);
        Expect(mesher.builds == 2 && cache.GetPublishCount(root) == 1 &&
                   replacement &&
                   replacement->GeometryInputs().Matches(
                       world.CaptureGeometryRenderInputs(
                           root, registry.GetDefinitionsCatalogSnapshot())),
               "replacement publishes actual edited geometry rather than the "
               "discarded predecessor");
      }
      continue;
    }
    if (scenario == 2 || scenario == 7 || scenario == 10)
    {
      Expect(cache.GetGeometryAwaitingLightCount() == 1 &&
                 cache.HasInflightMeshBuild(root) &&
                 cache.GetPublishCount(root) == 0 && mesher.builds == 1,
             "certified captured geometry survives later unready field or "
             "resource pressure");
      if (scenario == 2)
      {
        Expect(preparations == 0, "unready current field cannot prepare an "
                                  "obsolete captured resource");
        calculate();
      }
      if (scenario == 10)
      {
        Expect(preparations == 0,
               "calculated field without current demand settlement retains "
               "geometry without resource preparation");
        const auto *chunk = world.GetChunkManager().GetChunk(root);
        cache.GetRenderDemandStore().NoteLightCalculationSettled(
            root, cache.GetCaptureStore().WorldEpoch(), chunk->GetIncarnation(),
            chunk->GetLightFieldRevision(), chunk->GetContentRevision());
      }
      reject_resource = false;
      cache.ConsumeGpuApplyBacklog(world, registry, 8, 0, 0);
    }
    const auto current = world.CaptureRenderLightSnapshot(root);
    const auto binding = cache.GetPublishedRenderLightBinding(root);
    if (cache.GetPublishCount(root) != 1)
      std::cerr << "inflight-light scenario=" << scenario << " gpu=" << use_gpu
                << '\n';
    Expect(current && binding && mesher.builds == 1 &&
               cache.GetPublishCount(root) == 1 &&
               cache.GetChunkMeshRevision(root) == revision &&
               binding->Matches(cache.GetCaptureStore().WorldEpoch(),
                                binding->GeometryInputs(), *current) &&
               cache.GetMeshedLightRevision(root) ==
                   world.GetChunkManager()
                       .GetChunk(root)
                       ->GetLightFieldRevision() &&
               ChunkMeshCaptureAdmissionTestAccess::PublishedOriginalLight(
                   cache, root) == 0,
           "inflight light-only changes publish original geometry once with "
           "current physical field");
#if defined(SECTION_LIGHT_BINDING_DRIVER)
    if (use_gpu)
      ProbeCertifiedBinding(resources, binding);
#endif
    for (int tick = 0; tick < 8; ++tick)
      cache.RebuildDirtyChunksWithStats(world, registry, 8, 1, false, 0);
    Expect(mesher.builds == 1 && cache.GetPublishCount(root) == 1 &&
               !cache.HasPendingDirty(),
           "late-paired inflight geometry cannot acquire historical-light "
           "remesh debt");
  }
}

static void TestNeighborBoundaryGeometryInputs(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  for (const glm::ivec3 root : {glm::ivec3(0), glm::ivec3(-2, -4, 3)})
    for (int axis = 0; axis < 3; ++axis)
      for (int sign : {-1, 1})
      {
        UBlockWorld world;
        world.SetFluidDefinitions(definitions);
        world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
        world.SetBlock(root * CHUNK_SIZE + glm::ivec3(8), kStone);
        auto neighbor = root;
        neighbor[axis] += sign;
        world.SetBlock(neighbor * CHUNK_SIZE + glm::ivec3(8), kStone);
        const auto catalog = registry.GetDefinitionsCatalogSnapshot();
        const auto before = world.CaptureGeometryRenderInputs(root, catalog);
        const auto full = world.CaptureInputStamp(neighbor, false, true);
        world.SetBlock(neighbor * CHUNK_SIZE + glm::ivec3(7), kFire);
        const auto interior = world.CaptureGeometryRenderInputs(root, catalog);
        Expect(before.Matches(interior),
               "neighbor interior edit preserves exact face geometry inputs");
        Expect(!world.InputStampStillMatches(full),
               "full calculation input still rejects neighbor interior edits");
        auto legacy = before;
        for (auto &stamp : legacy.stamps)
          stamp.hasBoundaryContent = false;
        Expect(!legacy.Matches(interior), "legacy stamps without boundary "
                                          "evidence retain full-content guard");
        auto boundary = glm::ivec3(7);
        boundary[axis] = sign > 0 ? 1 : CHUNK_SIZE - 2;
        world.SetBlock(neighbor * CHUNK_SIZE + boundary, kStone);
        const auto second_plane =
            world.CaptureGeometryRenderInputs(root, catalog);
        Expect(!interior.Matches(second_plane),
               "distance-two neighbor geometry must invalidate the retained "
               "proof");
        boundary[axis] = sign > 0 ? 0 : CHUNK_SIZE - 1;
        world.SetBlock(neighbor * CHUNK_SIZE + boundary, kStone);
        const auto changed = world.CaptureGeometryRenderInputs(root, catalog);
        Expect(!second_plane.Matches(changed),
               "actual neighboring boundary edit rejects retained geometry");
        auto *chunk = world.GetChunkManager().GetChunk(neighbor);
        chunk->SetFluidLocal(boundary, FluidCellState::Flowing(3));
        Expect(
            !changed.Matches(world.CaptureGeometryRenderInputs(root, catalog)),
            "boundary fluid geometry participates in the same content proof");
        const auto fluid = world.CaptureGeometryRenderInputs(root, catalog);
        chunk->ClearFluidLocal(boundary);
        Expect(!fluid.Matches(world.CaptureGeometryRenderInputs(root, catalog)),
               "clearing boundary fluid also invalidates retained geometry");
        const auto resident = world.CaptureGeometryRenderInputs(root, catalog);
        world.GetChunkManager().RemoveChunk(neighbor);
        world.SetBlock(neighbor * CHUNK_SIZE + glm::ivec3(8), kStone);
        Expect(
            !resident.Matches(world.CaptureGeometryRenderInputs(root, catalog)),
            "neighbor reincarnation cannot reuse matching boundary clock "
            "values");
      }
}

static void TestPublishedLightRefreshLifecycle(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  TestNeighborBoundaryGeometryInputs(registry, definitions);
  const glm::ivec3 root(0);
  for (bool asynchronous : {false, true})
    for (int lifecycle = 0; lifecycle < 19; ++lifecycle)
    {
      UBlockWorld world;
      world.SetFluidDefinitions(definitions);
      world.SetLightingConfigurationIdentity(
          static_cast<uint64_t>(LightingMode::Full));
      world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      world.SetBlock({14, 8, 14}, kStone);
      if (lifecycle == 18)
        world.SetBlock({8, 0, 31}, kStone);
      CountingPhysicalMesher mesher;
      UChunkMeshCache cache;
      auto settings = MakeFrustumMeshSettings();
      settings.AsyncMeshing = asynchronous;
      settings.FrustumCulling = false;
      cache.SetRenderSettings(settings);
      cache.SetMesherBackend(&mesher);
      cache.SetMeshRebuildFocus(root, 4, 1);
      cache.SetRepairGuardWorld(&world);
      cache.SetPrepareRenderLightFn(
          [](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
             const SectionRenderLightSnapshot &light)
              -> std::shared_ptr<SectionRenderLightBinding>
          {
            auto binding = std::make_shared<TestRenderLightBinding>(
                epoch, geometry, light);
            binding->refreshable = true;
            return binding;
          });
      const auto calculate = [&]()
      {
        RelightJobSpec spec;
        spec.min_world_y = world.GetVerticalProfile().MinY;
        spec.max_world_y = world.GetVerticalProfile().MaxY;
        const auto field =
            UChunkRelightSnapshot::CaptureSections(world, spec, {root})
                .Compute(registry);
        Expect(field.inputs_complete && field.chunks.size() == 1,
               "field-refresh fixture computes actual complete physical "
               "transport");
        world.GetChunkManager().ReplaceComputedSectionLightVolume(
            root, *field.chunks[0].padded_light, true, true);
        Expect(world.RecordLightCalculation(
                   field, registry.GetDefinitionsCatalogSnapshot()) == 1,
               "field-refresh fixture installs actual calculation certificate");
      };
      const auto build = [&]()
      {
        const auto *chunk = world.GetChunkManager().GetChunk(root);
        auto &demand = cache.GetRenderDemandStore();
        const auto epoch = cache.GetCaptureStore().WorldEpoch();
        const auto revision = cache.GetChunkMeshRevision(root);
        demand.NoteDemand(root, revision, chunk->GetLightFieldRevision(), 0,
                          1.0, epoch, chunk->GetIncarnation());
        demand.NoteLightCalculationSettled(root, epoch, chunk->GetIncarnation(),
                                           chunk->GetLightFieldRevision(),
                                           chunk->GetContentRevision());
        if (asynchronous)
          Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
                     cache, world, root, revision)
                     .has_value(),
                 "field-refresh fixture captures actual async geometry");
        const auto tick =
            cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
        Expect(asynchronous
                   ? tick.Scheduled == 1 &&
                         cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5))
                   : tick.SyncRebuilt == 1,
               "field-refresh fixture builds actual geometry");
        cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
      };
      calculate();
      cache.MarkDirtyPriority(root);
      build();
      auto binding = std::dynamic_pointer_cast<TestRenderLightBinding>(
          cache.GetPublishedRenderLightBinding(root));
      const auto published = cache.GetPublishCount(root);
      const auto geometry_revision = cache.GetChunkMeshRevision(root);
      const auto old_light = cache.GetMeshedLightRevision(root);
      Expect(binding && published == 1 && mesher.builds == 1,
             "field-refresh fixture starts with one actual published geometry "
             "artifact");
      for (int hint = 0; hint < 4; ++hint)
        cache.MarkDirtyPriority(
            root, MeshRevisionBumpReason::PriorityWorldCoreRepair);
      Expect(!cache.HasInflightMeshBuild(root) && !cache.HasPendingDirty() &&
                 cache.GetPublishedLightRefreshPendingCount() == 0 &&
                 cache.GetPublishedLightRefreshCount() == 0 &&
                 mesher.builds == 1 && cache.GetPublishCount(root) == published,
             "current physical publication does not acquire a new work owner "
             "for repair hints");
      world.SetBlock(lifecycle == 18   ? glm::ivec3(8, 8, 18)
                     : lifecycle == 17 ? glm::ivec3(18, 8, 18)
                                       : glm::ivec3(16, 8, 16),
                     lifecycle == 17 ? kStone : kFire);
      cache.MarkDirtyPriority(
          root, MeshRevisionBumpReason::PriorityRelitInstallRepair);
      cache.ConsumeGpuApplyBacklog(world, registry, 8, 0, 0);
      Expect(cache.IsPublishedLightRefreshPending(root) &&
                 cache.HasPendingDirty() && !cache.IsChunkMeshDirty(root) &&
                 mesher.builds == 1 &&
                 cache.GetMeshedLightRevision(root) == old_light,
             "unready field update owns work without capturing or rebuilding "
             "geometry");
      calculate();
      if (lifecycle == 17)
        Expect(
            world.GetChunkManager().GetChunk(root)->GetLightFieldRevision() ==
                old_light,
            "proof-only refresh establishes calculation without changing field "
            "bytes or revision");
      switch (lifecycle)
      {
      case 1:
        cache.RemoveChunk(root);
        break;
      case 2:
        cache.RemoveColumn(root, 0, 0);
        break;
      case 3:
        cache.GetCaptureStore().BumpWorldEpoch();
        break;
      case 4:
        world.SetBlock({13, 8, 14}, kStone);
        cache.MarkDirtyPriority(root);
        break;
      case 5:
        world.GetChunkManager().RemoveChunk(root);
        world.SetBlock({14, 8, 14}, kStone);
        break;
      case 6:
        cache.CancelAsyncMeshWork();
        break;
      case 7:
        cache.CancelAsyncInFlightKeepDirty();
        break;
      case 8:
        cache.CancelInFlightOutsideHorizontalRadius({100, 0, 100}, 1);
        break;
      case 9:
        cache.SetPrepareRenderLightFn({});
        break;
      case 10:
        settings.Lighting = LightingMode::Flat;
        cache.SetRenderSettings(settings);
        break;
      case 11:
        binding->reject_refresh = true;
        for (int tick = 0; tick < 8; ++tick)
        {
          cache.MarkDirtyPriority(
              root, MeshRevisionBumpReason::PriorityRelitInstallRepair);
          cache.ConsumeGpuApplyBacklog(world, registry, 8, 0, 0);
          Expect(cache.GetPublishedLightRefreshPendingCount() == 1 &&
                     cache.GetMeshedLightRevision(root) == old_light &&
                     mesher.builds == 1 &&
                     cache.GetPublishCount(root) == published &&
                     !cache.IsChunkMeshDirty(root),
                 "unavailable field resource retains one fair retry without "
                 "remeshing");
          Expect(!cache.IsPublishedMeshCurrentForInputs(root, world),
                 "retained predecessor cannot certify an unavailable "
                 "replacement field");
        }
        binding->reject_refresh = false;
        break;
      case 12:
        binding->on_refresh = [&]() { cache.CancelAsyncMeshWork(); };
        break;
      case 13:
        binding->on_refresh = [&]() { world.SetBlock({13, 8, 14}, kStone); };
        break;
      case 14:
        binding->on_refresh = [&]()
        {
          cache.RemoveChunk(root);
          world.GetChunkManager().RemoveChunk(root);
          world.SetBlock({14, 8, 14}, kStone);
        };
        break;
      case 15:
        binding->on_refresh = [&]() { binding->live = false; };
        break;
      case 16:
        binding->on_refresh = [&]()
        {
          cache.CancelAsyncMeshWork();
          Expect(cache.RequestPublishedLightRefresh(world, registry, root),
                 "cancelled field callback can admit a new request for the "
                 "same publication");
        };
        break;
      default:
        break;
      }
      cache.ConsumeGpuApplyBacklog(world, registry, 8, 0, 0);
      if (lifecycle == 16)
      {
        Expect(
            cache.GetPublishedLightRefreshPendingCount() == 1 &&
                cache.GetPublishedLightRefreshCount() == 0 &&
                cache.GetMeshedLightRevision(root) == old_light,
            "cancelled stack owner cannot consume a replacement field request");
        binding->on_refresh = {};
        cache.ConsumeGpuApplyBacklog(world, registry, 8, 0, 0);
      }
      const bool updated = lifecycle == 0 || lifecycle == 11 ||
                           lifecycle == 16 || lifecycle == 17 ||
                           lifecycle == 18;
      if (cache.GetPublishedLightRefreshCount() != uint64_t(updated))
        std::cerr << "field-refresh lifecycle=" << lifecycle
                  << " async=" << asynchronous << '\n';
      Expect(cache.GetPublishedLightRefreshPendingCount() == 0 &&
                 cache.GetPublishedLightRefreshCount() == uint64_t(updated) &&
                 mesher.builds == 1,
             "field-refresh cancellation source identity and callback "
             "lifecycle are respected");
      if (lifecycle == 13 || lifecycle == 15)
        Expect(!cache.IsPublishedMeshCurrentForInputs(root, world),
               "changed geometry or dead binding cannot certify a physical "
               "publication after field refresh");
      if (lifecycle == 1 || lifecycle == 2 || lifecycle == 14)
      {
        Expect(cache.GetGreedyCacheSize() == 0 &&
                   cache.GetPublishCount(root) == 0,
               "eviction cannot republish a cancelled physical field");
        continue;
      }
      Expect(cache.GetPublishCount(root) == published,
             "field refresh never changes the geometry publication count");
      if (updated)
      {
        if (lifecycle == 0)
          Expect(cache.IsPublishedMeshCurrentForInputs(root, world),
                 "light-only refresh leaves physical publication current "
                 "without rebuilding legacy baked-light stamps");
        const auto current = world.CaptureRenderLightSnapshot(root);
        const auto *chunk = world.GetChunkManager().GetChunk(root);
        Expect(
            current && binding == cache.GetPublishedRenderLightBinding(root) &&
                binding->Matches(cache.GetCaptureStore().WorldEpoch(),
                                 binding->GeometryInputs(), *current) &&
                cache.GetMeshedLightRevision(root) ==
                    chunk->GetLightFieldRevision() &&
                cache.GetChunkMeshRevision(root) == geometry_revision &&
                cache.GetRenderDemandStore().Find(root)->published_light_rev ==
                    chunk->GetLightFieldRevision(),
            "field refresh updates compatible resource and demand while "
            "preserving geometry identity");
      }
      else
        Expect(cache.GetMeshedLightRevision(root) == old_light,
               "cancelled or superseded field work cannot advance published "
               "light metadata");
      if (lifecycle == 4 || lifecycle == 5 || lifecycle == 13)
      {
        binding->on_refresh = {};
        Expect(cache.IsChunkMeshDirty(root),
               "actual geometry edit keeps a rebuild obligation");
        calculate();
        build();
        Expect(mesher.builds == 2 &&
                   cache.GetPublishCount(root) == published + 1,
               "actual changed geometry rebuilds and publishes after field "
               "work retires");
      }
    }
}

static void TestPublishedLightRefreshFairness(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  for (bool cancel_batch : {false, true})
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    CountingPhysicalMesher mesher;
    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.AsyncMeshing = false;
    settings.FrustumCulling = false;
    cache.SetRenderSettings(settings);
    cache.SetMesherBackend(&mesher);
    cache.SetRepairGuardWorld(&world);
    cache.SetPrepareRenderLightFn(
        [](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
           const SectionRenderLightSnapshot &light)
            -> std::shared_ptr<SectionRenderLightBinding>
        {
          auto binding =
              std::make_shared<TestRenderLightBinding>(epoch, geometry, light);
          binding->refreshable = true;
          return binding;
        });
    const auto calculate = [&](glm::ivec3 coord)
    {
      RelightJobSpec spec;
      spec.min_world_y = world.GetVerticalProfile().MinY;
      spec.max_world_y = world.GetVerticalProfile().MaxY;
      const auto field =
          UChunkRelightSnapshot::CaptureSections(world, spec, {coord})
              .Compute(registry);
      Expect(field.inputs_complete && field.chunks.size() == 1,
             "field fairness fixture computes actual complete field");
      world.GetChunkManager().ReplaceComputedSectionLightVolume(
          coord, *field.chunks[0].padded_light, true, true);
      Expect(world.RecordLightCalculation(
                 field, registry.GetDefinitionsCatalogSnapshot()) == 1,
             "field fairness fixture installs actual calculation proof");
    };
    std::array<glm::ivec3, 9> coords;
    for (size_t n = 0; n < coords.size(); ++n)
    {
      coords[n] = {static_cast<int>(n) * 4, 0, 0};
      world.SetBlock(coords[n] * CHUNK_SIZE + glm::ivec3(14, 8, 14), kStone);
      calculate(coords[n]);
      cache.MarkDirtyPriority(coords[n]);
      cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
      Expect(cache.GetPublishCount(coords[n]) == 1,
             "field fairness fixture publishes actual synchronous geometry");
    }
    for (const auto coord : coords)
    {
      world.SetBlock(coord * CHUNK_SIZE + glm::ivec3(16, 8, 16), kFire);
      for (int duplicate = 0; duplicate < 8; ++duplicate)
        cache.MarkDirtyPriority(
            coord, MeshRevisionBumpReason::PriorityRelitInstallRepair);
    }
    Expect(cache.GetPublishedLightRefreshPendingCount() == coords.size() &&
               cache.GetPublishedLightRefreshPendingCount() <=
                   cache.GetGreedyCacheSize() &&
               mesher.builds == coords.size(),
           "field queue deduplicates requests and stays bounded by resident "
           "published artifacts");
    const auto target = coords.back();
    const auto before = cache.GetMeshedLightRevision(target);
    calculate(target);
    auto binding = std::dynamic_pointer_cast<TestRenderLightBinding>(
        cache.GetPublishedRenderLightBinding(target));
    if (cancel_batch)
      binding->on_refresh = [&]() { cache.CancelAsyncMeshWork(); };
    for (int round = 0; round < 3; ++round)
      cache.ConsumeGpuApplyBacklog(world, registry, 4, 0, 0);
    Expect(mesher.builds == coords.size() && cache.GetPublishCount(target) == 1,
           "field queue pressure never schedules geometry work");
    if (cancel_batch)
      Expect(cache.GetPublishedLightRefreshPendingCount() == 0 &&
                 cache.GetPublishedLightRefreshCount() == 0 &&
                 cache.GetMeshedLightRevision(target) == before,
             "callback cancellation clears a multi-entry batch without "
             "consuming cancelled work");
    else
    {
      Expect(cache.GetPublishedLightRefreshCount() == 1 &&
                 cache.GetPublishedLightRefreshPendingCount() ==
                     coords.size() - 1 &&
                 cache.GetMeshedLightRevision(target) > before,
             "ready field behind eight unready peers publishes within three "
             "bounded rounds");
      for (size_t n = 0; n + 1 < coords.size(); ++n)
        cache.RemoveColumn(coords[n], 0, 0);
      Expect(cache.GetPublishedLightRefreshPendingCount() == 0 &&
                 cache.GetGreedyCacheSize() == 1,
             "column eviction releases every corresponding field queue ticket");
    }
  }
}

static void TestGeometryWaitsForPhysicalLight(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions, bool use_gpu = false,
    bool synchronous = false, bool deferred = false,
    bool extract_batches = false)
{
  using namespace cutum;
  const glm::ivec3 root(0);
  for (int lifecycle = 0; lifecycle < 16; ++lifecycle)
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetLightingConfigurationIdentity(
        static_cast<uint64_t>(LightingMode::Full));
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    world.SetBlock({4, 4, 4}, kStone);
    auto *chunk = world.GetChunkManager().GetChunk(root);
    SectionRenderLightBindings resources;
    CountingPhysicalMesher mesher;
    mesher.deferred = deferred;
    mesher.extract_batches = extract_batches;
    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.AsyncMeshing = !synchronous;
    if (deferred)
      settings.GpuPackedMeshing = false;
    settings.FrustumCulling = false;
    cache.SetRenderSettings(settings);
    cache.SetMesherBackend(&mesher);
    cache.SetMeshRebuildFocus(root, 4, 1);
    if (use_gpu)
      Expect(resources.BeginWorld(
                 cache.GetCaptureStore().WorldEpoch(),
                 world
                     .CaptureGeometryRenderInputs(
                         root, registry.GetDefinitionsCatalogSnapshot())
                     .world_geometry_identity,
                 4, 4u * SectionLightVolume::CellCount, 4),
             "awaiting-field GPU owner initializes");
    cache.SetPrepareRenderLightFn(
        [&](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
            const SectionRenderLightSnapshot &light)
            -> std::shared_ptr<SectionRenderLightBinding>
        {
          if (use_gpu)
            return resources.Prepare(epoch, geometry, light);
          return std::make_shared<TestRenderLightBinding>(epoch, geometry,
                                                          light);
        });
    cache.MarkDirtyPriority(root);
    const auto geom = cache.GetChunkMeshRevision(root);
    const auto epoch = cache.GetCaptureStore().WorldEpoch();
    auto &demand = cache.GetRenderDemandStore();
    demand.NoteDemand(root, geom, chunk->GetLightFieldRevision(), 0, 1.0, epoch,
                      chunk->GetIncarnation());
    Expect(
        ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(cache, world,
                                                                root, geom)
            .has_value(),
        "awaiting-field fixture captures actual geometry before calculation");
    const auto initial =
        cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0);
    Expect(synchronous
               ? initial.SyncRebuilt == 1
               : initial.Scheduled == 1 &&
                     cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
           "actual builder computes geometry before the physical field");
    cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
    Expect(mesher.builds == 1 && cache.HasInflightMeshBuild(root) &&
               !cache.IsChunkMeshDirty(root) &&
               !cache.HasDrawableGreedyMesh(root) &&
               cache.GetGeometryAwaitingLightCount() == 1 &&
               cache.GetGeometryAwaitingLightBytes() > 0,
           "completed geometry keeps one owner while awaiting physical light");
    for (int tick = 0; tick < 8; ++tick)
    {
      if (synchronous && tick == 1)
        cache.SetRenderSettings(settings);
      if (tick == 2)
        cache.MarkDirtyPriority(
            root, MeshRevisionBumpReason::PriorityRelitInstallRepair);
      if (tick == 4)
        cache.ConsumeGpuApplyBacklog(world, registry, 8, 0, 0);
      if (tick == 6)
        cache.DrainAsyncMeshResults(world, registry, 8);
      const auto next =
          cache.RebuildDirtyChunksWithStats(world, registry, 8, 1, false, 0);
      Expect(next.Scheduled == 0 && mesher.builds == 1 &&
                 cache.GetChunkMeshRevision(root) == geom &&
                 cache.HasPendingDirty(),
             "waiting for field cannot resubmit geometry or lose durable work");
    }
    switch (lifecycle)
    {
    case 1:
      world.SetBlock({5, 4, 4}, kStone);
      cache.MarkDirtyPriority(root);
      break;
    case 2:
      cache.RemoveChunk(root);
      break;
    case 3:
      cache.RemoveColumn(root, 0, 0);
      break;
    case 4:
      cache.GetCaptureStore().BumpWorldEpoch();
      break;
    case 5:
      cache.CancelAsyncMeshWork();
      break;
    case 6:
      cache.CancelAsyncInFlightKeepDirty();
      break;
    case 7:
      cache.CancelInFlightOutsideHorizontalRadius({100, 0, 100}, 1);
      break;
    case 8:
      cache.SetPrepareRenderLightFn({});
      break;
    case 9:
      cache.MarkAllDirty();
      break;
    case 10:
      world.SetBlock({5, 4, 4}, kStone);
      break;
    case 11:
      settings.Lighting = LightingMode::Flat;
      cache.SetRenderSettings(settings);
      break;
    case 12:
      settings.AsyncMeshing = synchronous;
      cache.SetRenderSettings(settings);
      break;
    case 13:
      world.GetChunkManager().RemoveChunk(root);
      break;
    case 14:
    {
      const auto previous_incarnation = chunk->GetIncarnation();
      world.GetChunkManager().RemoveChunk(root);
      world.SetBlock({4, 4, 4}, kStone);
      chunk = world.GetChunkManager().GetChunk(root);
      Expect(chunk->GetIncarnation() != previous_incarnation,
             "same-coordinate reload has a distinct geometry incarnation");
      break;
    }
    default:
      break;
    }
    if (lifecycle == 15)
    {
      cache.GetCaptureStore().SetLightInputsPolicy(
          MeshLightInputs::PhysicalField);
      cache.GetCaptureStore().SetLightInputsPolicy(MeshLightInputs::Baked);
    }
    if (lifecycle == 11 || lifecycle == 12)
    {
      Expect(cache.GetGeometryAwaitingLightCount() == 0 &&
                 cache.GetGeometryAwaitingLightBytes() == 0 &&
                 cache.IsChunkMeshDirty(root) &&
                 cache.GetPublishCount(root) == 0,
             "render policy change releases pending owner and preserves retry");
      continue;
    }
    if (lifecycle == 13)
    {
      cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
      Expect(
          cache.GetGeometryAwaitingLightCount() == 0 &&
              cache.GetGeometryAwaitingLightBytes() == 0 &&
              cache.GetPublishCount(root) == 0,
          "world eviction retires pending geometry without cache notification");
      continue;
    }
    RelightJobSpec spec;
    spec.min_world_y = world.GetVerticalProfile().MinY;
    spec.max_world_y = world.GetVerticalProfile().MaxY;
    const auto field =
        UChunkRelightSnapshot::CaptureSections(world, spec, {root})
            .Compute(registry);
    Expect(field.inputs_complete && field.chunks.size() == 1,
           "awaiting-field fixture computes real physical output");
    chunk->ReplaceComputedLightVolume(*field.chunks[0].padded_light, true,
                                      true);
    Expect(world.RecordLightCalculation(
               field, registry.GetDefinitionsCatalogSnapshot()) == 1,
           "awaiting-field fixture installs actual calculation proof");
    demand.NoteDemand(root, geom, chunk->GetLightFieldRevision(), 0, 2.0, epoch,
                      chunk->GetIncarnation());
    demand.NoteLightCalculationSettled(root, epoch, chunk->GetIncarnation(),
                                       chunk->GetLightFieldRevision(),
                                       chunk->GetContentRevision());
    if (synchronous)
      cache.DrainPendingGpuMeshes(world, registry, 8, 0);
    else
      cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
    if (lifecycle)
    {
      if (cache.GetGeometryAwaitingLightCount() != 0 ||
          cache.GetGeometryAwaitingLightBytes() != 0 ||
          cache.GetPublishCount(root) != 0)
        std::cerr << "awaiting-field lifecycle=" << lifecycle << '\n';
      Expect(cache.GetGeometryAwaitingLightCount() == 0 &&
                 cache.GetGeometryAwaitingLightBytes() == 0 &&
                 cache.GetPublishCount(root) == 0 && mesher.builds == 1,
             "edit eviction cancellation or epoch change retires pending "
             "geometry");
      if (lifecycle == 1 || lifecycle == 10 || lifecycle == 14)
        Expect(cache.IsChunkMeshDirty(root) &&
                   cache.GetChunkMeshRevision(root) > geom,
               "actual geometry change advances the retry target after "
               "validation");
      continue;
    }
    const auto binding = cache.GetPublishedRenderLightBinding(root);
    const auto current = world.CaptureRenderLightSnapshot(root);
    Expect(mesher.builds == 1 && cache.GetPublishCount(root) == 1 &&
               cache.HasDrawableGreedyMesh(root) &&
               !cache.HasProvisionalLightPreview(root) && binding && current &&
               binding->Matches(epoch, binding->GeometryInputs(), *current) &&
               cache.GetMeshedLightRevision(root) ==
                   chunk->GetLightFieldRevision(),
           "computed field publishes retained actual geometry without a second "
           "build");
    Expect(demand.Find(root)->published_geom_rev == geom &&
               demand.Find(root)->published_light_rev ==
                   chunk->GetLightFieldRevision(),
           "late-paired publication satisfies actual render-demand revisions");
#if defined(SECTION_LIGHT_BINDING_DRIVER)
    if (use_gpu)
      ProbeCertifiedBinding(resources, binding);
#endif
    for (int tick = 0; tick < 8; ++tick)
    {
      cache.RebuildDirtyChunksWithStats(world, registry, 8, 1, false, 0);
      Expect(mesher.builds == 1 && cache.GetPublishCount(root) == 1 &&
                 !cache.HasPendingDirty(),
             "late physical pairing cannot dirty historical baked halo inputs");
    }
  }
}

static void TestGeometryLightQueueCapacity(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  const auto result_credits_before =
      UPipelineAdmission::Get().ResultPendingBytes();
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  CountingPhysicalMesher mesher;
  UChunkMeshCache cache;
  auto settings = MakeFrustumMeshSettings();
  settings.AsyncMeshing = true;
  settings.FrustumCulling = false;
  cache.SetRenderSettings(settings);
  cache.SetMesherBackend(&mesher);
  cache.SetPrepareRenderLightFn(
      [](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
         const SectionRenderLightSnapshot &light)
          -> std::shared_ptr<SectionRenderLightBinding>
      {
        return std::make_shared<TestRenderLightBinding>(epoch, geometry, light);
      });
  const auto schedule = [&](glm::ivec3 coord)
  {
    cache.SetMeshRebuildFocus(coord, 4, 1);
    cache.MarkDirtyPriority(coord);
    Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
               cache, world, coord, cache.GetChunkMeshRevision(coord))
               .has_value(),
           "bounded awaiting-field fixture captures actual source");
    Expect(cache.RebuildDirtyChunksWithStats(world, registry, 0, 1, false, 0)
                       .Scheduled == 1 &&
               cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
           "bounded awaiting-field fixture completes actual mesh job");
    cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
  };
  for (int n = 0; n < 33; ++n)
  {
    const glm::ivec3 coord(n * 3, 0, 0);
    world.SetBlock(coord * CHUNK_SIZE + glm::ivec3(4), kStone);
    schedule(coord);
    if (cache.GetGeometryAwaitingLightCount() != std::min<size_t>(n + 1, 32))
    {
      std::cerr << "awaiting capacity n=" << n
                << " count=" << cache.GetGeometryAwaitingLightCount()
                << " bytes=" << cache.GetGeometryAwaitingLightBytes()
                << " builds=" << mesher.builds
                << " active=" << cache.HasInflightMeshBuild(coord)
                << " dirty=" << cache.IsChunkMeshDirty(coord) << '\n';
      UJobStageTrace::ForEachNewest(
          16,
          [](const JobStageSpan &span, void *)
          {
            std::cerr << "capacity lifecycle coord=" << span.cx << ','
                      << span.cy << ',' << span.cz << " job=" << span.job_id
                      << " stage=" << UJobStageTrace::StageName(span.stage)
                      << " terminal="
                      << UJobStageTrace::TerminalReasonName(
                             span.terminal_reason)
                      << '\n';
          },
          nullptr);
    }
    Expect(cache.GetGeometryAwaitingLightCount() ==
                   std::min<size_t>(n + 1, 32) &&
               cache.GetGeometryAwaitingLightBytes() <= 32u * 1024u * 1024u,
           "awaiting-field result count and retained payload stay bounded");
  }
  const glm::ivec3 last(96, 0, 0);
  Expect(!cache.IsGeometryAwaitingLight(last) && cache.IsChunkMeshDirty(last),
         "full awaiting-field queue preserves the unadmitted geometry retry");
  cache.RemoveChunk({0, 0, 0});
  schedule(last);
  Expect(cache.IsGeometryAwaitingLight(last) &&
             cache.GetGeometryAwaitingLightCount() == 32,
         "eviction releases queue capacity for a waiting successor");
  RelightJobSpec spec;
  spec.min_world_y = world.GetVerticalProfile().MinY;
  spec.max_world_y = world.GetVerticalProfile().MaxY;
  const auto field = UChunkRelightSnapshot::CaptureSections(world, spec, {last})
                         .Compute(registry);
  auto *chunk = world.GetChunkManager().GetChunk(last);
  Expect(field.inputs_complete && field.chunks.size() == 1,
         "last queued geometry obtains actual physical inputs");
  chunk->ReplaceComputedLightVolume(*field.chunks[0].padded_light, true, true);
  Expect(world.RecordLightCalculation(
             field, registry.GetDefinitionsCatalogSnapshot()) == 1,
         "last queued geometry receives current physical proof");
  auto &demand = cache.GetRenderDemandStore();
  const auto epoch = cache.GetCaptureStore().WorldEpoch();
  demand.NoteDemand(last, cache.GetChunkMeshRevision(last),
                    chunk->GetLightFieldRevision(), 0, 1.0, epoch,
                    chunk->GetIncarnation());
  demand.NoteLightCalculationSettled(last, epoch, chunk->GetIncarnation(),
                                     chunk->GetLightFieldRevision(),
                                     chunk->GetContentRevision());
  for (int tick = 0; tick < 8; ++tick)
    cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
  Expect(cache.GetPublishCount(last) == 1 && mesher.builds == 34 &&
             cache.GetGeometryAwaitingLightCount() == 31,
         "round-robin field admission cannot starve behind unready geometry");
  const auto notified =
      ChunkMeshCaptureAdmissionTestAccess::AwaitingLightBack(cache);
  const auto retained_bytes = cache.GetGeometryAwaitingLightBytes();
  Expect(cache.NotifyGeometryLightReady(notified),
         "an untrusted hint only addresses an existing retained result");
  cache.RebuildDirtyChunksWithStats(world, registry, 4, 0, false, 0);
  Expect(cache.GetPublishCount(notified) == 0 && mesher.builds == 34 &&
             cache.GetGeometryAwaitingLightCount() == 31 &&
             cache.GetGeometryAwaitingLightBytes() == retained_bytes,
         "a completion hint cannot publish without actual current field proof");
  const auto notified_field =
      UChunkRelightSnapshot::CaptureSections(world, spec, {notified})
          .Compute(registry);
  auto *notified_chunk = world.GetChunkManager().GetChunk(notified);
  Expect(notified_field.inputs_complete && notified_field.chunks.size() == 1,
         "completion hint fixture computes actual queued section field");
  notified_chunk->ReplaceComputedLightVolume(
      *notified_field.chunks[0].padded_light, true, true);
  Expect(world.RecordLightCalculation(
             notified_field, registry.GetDefinitionsCatalogSnapshot()) == 1,
         "completion hint fixture installs actual current provenance");
  demand.NoteDemand(notified, cache.GetChunkMeshRevision(notified),
                    notified_chunk->GetLightFieldRevision(), 0, 1.0, epoch,
                    notified_chunk->GetIncarnation());
  demand.NoteLightCalculationSettled(notified, epoch,
                                     notified_chunk->GetIncarnation(),
                                     notified_chunk->GetLightFieldRevision(),
                                     notified_chunk->GetContentRevision());
  Expect(cache.NotifyGeometryLightReady(notified) &&
             cache.NotifyGeometryLightReady(notified),
         "field completion coalesces on one existing geometry owner");
  cache.RebuildDirtyChunksWithStats(world, registry, 4, 0, false, 0);
  Expect(cache.GetPublishCount(notified) == 1 && mesher.builds == 34 &&
             cache.GetGeometryAwaitingLightCount() == 30,
         "field completion services retained tail geometry in the next bounded "
         "drain");
  const auto fair =
      ChunkMeshCaptureAdmissionTestAccess::AwaitingLightBack(cache);
  const auto noisy =
      ChunkMeshCaptureAdmissionTestAccess::AwaitingLightFront(cache);
  Expect(fair != noisy, "fairness fixture has distinct actual retained owners");
  const auto fair_field =
      UChunkRelightSnapshot::CaptureSections(world, spec, {fair})
          .Compute(registry);
  auto *fair_chunk = world.GetChunkManager().GetChunk(fair);
  Expect(fair_field.inputs_complete && fair_field.chunks.size() == 1,
         "ordinary retained tail obtains actual physical light");
  fair_chunk->ReplaceComputedLightVolume(*fair_field.chunks[0].padded_light,
                                         true, true);
  Expect(world.RecordLightCalculation(
             fair_field, registry.GetDefinitionsCatalogSnapshot()) == 1,
         "ordinary retained tail installs current light proof");
  demand.NoteDemand(fair, cache.GetChunkMeshRevision(fair),
                    fair_chunk->GetLightFieldRevision(), 0, 1.0, epoch,
                    fair_chunk->GetIncarnation());
  demand.NoteLightCalculationSettled(fair, epoch, fair_chunk->GetIncarnation(),
                                     fair_chunk->GetLightFieldRevision(),
                                     fair_chunk->GetContentRevision());
  for (int tick = 0; tick < 64 && cache.GetPublishCount(fair) == 0; ++tick)
  {
    Expect(cache.NotifyGeometryLightReady(noisy),
           "repeated hint retains its existing owner");
    cache.RebuildDirtyChunksWithStats(world, registry, 1, 0, false, 0);
  }
  Expect(cache.GetPublishCount(fair) == 1 &&
             cache.GetPublishCount(noisy) == 0 && mesher.builds == 34 &&
             cache.GetGeometryAwaitingLightCount() == 29,
         "one-check drains preserve ordinary progress under repeated unready "
         "hints");
  cache.CancelAsyncMeshWork();
  Expect(cache.GetGeometryAwaitingLightCount() == 0 &&
             cache.GetGeometryAwaitingLightBytes() == 0 &&
             UPipelineAdmission::Get().ResultPendingBytes() ==
                 result_credits_before,
         "cancellation releases every retained result and capacity charge");
  Expect(!cache.NotifyGeometryLightReady(notified),
         "field completion cannot create a detached owner after cancellation");
}

#if defined(SECTION_LIGHT_BINDING_DRIVER)
static void TestGpuStagingSlotOwnership()
{
  using namespace cutum;
  UGpuMeshSlotAllocator allocator;
  Expect(allocator.Init(2),
         "staging ownership fixture initializes actual GL storage");
  const auto first =
      allocator.CaptureStagingSlotKey(allocator.AllocateStagingSlot(false));
  Expect(first.IsValid() && allocator.OwnsStagingSlot(first),
         "allocated staging slot receives an exact ownership key");
  Expect(allocator.ReleaseStagingSlot(first),
         "staging key releases its own allocation");
  allocator.FreeSlotByIndex(first.Index());
  Expect(allocator.GetFreeSlotCount() == 2 &&
             !allocator.ReleaseStagingSlot(first),
         "repeated staging release cannot duplicate free-list entries");
  const auto second =
      allocator.CaptureStagingSlotKey(allocator.AllocateStagingSlot(false));
  Expect(second.Index() == first.Index() &&
             second.Generation() != first.Generation() &&
             !allocator.ReleaseStagingSlot(first) &&
             allocator.OwnsStagingSlot(second),
         "old staging generation cannot free a reused numeric index");
  allocator.BindCommittedSlot({0, 0, 0}, second.Index());
  Expect(!allocator.ReleaseStagingSlot(second) &&
             allocator.HasSlot({0, 0, 0}) &&
             !allocator.CaptureStagingSlotKey(second.Index()).IsValid(),
         "staging cleanup cannot retire a committed live image");
  allocator.Shutdown();
  Expect(!allocator.ReleaseStagingSlot(second) &&
             allocator.GetSlotByIndex(0) == nullptr,
         "shutdown invalidates keys without indexing released storage");
  UGpuMeshSlotAllocator replacement;
  Expect(replacement.Init(2),
         "replacement allocator initializes actual GL storage");
  const auto fresh =
      replacement.CaptureStagingSlotKey(replacement.AllocateStagingSlot(false));
  Expect(fresh.Index() == first.Index() &&
             fresh.Generation() == first.Generation() &&
             !replacement.ReleaseStagingSlot(first) &&
             replacement.OwnsStagingSlot(fresh),
         "allocator replacement cannot alias a retained index and generation");
  replacement.Shutdown();
}

static void TestGpuReadbackOwnership()
{
  using namespace cutum;
  UGpuMeshPipeline pipeline;
  Expect(pipeline.Init(2),
         "readback fixture initializes actual pipeline storage");
  auto first = GpuMeshPipelineReadbackTestAccess::Acquire(pipeline);
  auto stale = first;
  auto old_storage = first;
  Expect(pipeline.OwnsReadbackSlot(first),
         "actual readback reservation owns its key");
  pipeline.ReleaseReadbackSlot(first);
  auto current = GpuMeshPipelineReadbackTestAccess::Acquire(pipeline);
  Expect(current.pboIndex == stale.pboIndex &&
             current.readbackKey.Generation() != stale.readbackKey.Generation(),
         "readback numeric index reuse advances its own generation");
  pipeline.ReleaseReadbackSlot(stale);
  Expect(pipeline.OwnsReadbackSlot(current),
         "old readback release preserves reused PBO");
  Expect(
      pipeline.GetAllocator().Init(2) && pipeline.OwnsReadbackSlot(current),
      "geometry storage reset does not revoke an independent readback owner");
  pipeline.ReleaseReadbackSlot(current);
  GpuMeshPipelineReadbackTestAccess::ResetStorage(pipeline);
  auto fresh = GpuMeshPipelineReadbackTestAccess::Acquire(pipeline);
  Expect(fresh.pboIndex == old_storage.pboIndex &&
             fresh.readbackKey.Generation() ==
                 old_storage.readbackKey.Generation(),
         "actual readback replacement exercises equal numeric index and "
         "generation");
  pipeline.ReleaseReadbackSlot(old_storage);
  Expect(pipeline.OwnsReadbackSlot(fresh),
         "old readback storage cannot release replacement storage");
  pipeline.ReleaseReadbackSlot(fresh);
  std::array<UGpuMeshPipeline::GpuApplyTicket, UGpuMeshPipeline::kReadbackRing>
      tickets;
  for (auto &ticket : tickets)
  {
    ticket = GpuMeshPipelineReadbackTestAccess::Acquire(pipeline);
    Expect(pipeline.OwnsReadbackSlot(ticket),
           "all bounded PBO slots remain reservable");
  }
  Expect(!pipeline.HasFreeReadbackSlot(),
         "actual readback pool observes its hard capacity");
  for (auto &ticket : tickets)
    pipeline.ReleaseReadbackSlot(ticket);
  pipeline.Shutdown();
}

static void TestGpuTicketStorageReplacement(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  world.SetBlock({4, 4, 4}, kStone);
  UChunkMeshCache cache;
  auto settings = MakeFrustumMeshSettings();
  settings.AsyncMeshing = true;
  cache.SetRenderSettings(settings);
  cache.MarkDirtyPriority({0, 0, 0});
  const auto snapshot = ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(
      cache, world, {0, 0, 0});
  ChunkMeshCaptureAdmissionTestAccess::QueuePacked(cache, snapshot, registry);
  auto &ticket = ChunkMeshCaptureAdmissionTestAccess::KickOnly(cache, registry);
  glFinish();
  auto old_ticket = ticket;
  auto *pipeline = cache.GetGpuMeshPipeline();
  auto &allocator = pipeline->GetAllocator();
  Expect(allocator.Init(2),
         "active compute ticket sees replacement geometry storage");
  const auto replacement =
      allocator.CaptureStagingSlotKey(allocator.AllocateStagingSlot(false));
  std::vector<uint8_t> sentinel(UGpuMeshSlotAllocator::kBytesPerSlot, 0x5a);
  auto observed = sentinel;
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, allocator.GetQuadSsbo());
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sentinel.size(),
                  sentinel.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  uint32_t ignored = 0;
  const auto status =
      ticket.awaitingCounters
          ? pipeline->TryCompleteCountersAndEmit(ticket, registry, 0)
          : pipeline->TryFinishComputePasses(ticket, registry, ignored, nullptr,
                                             nullptr, nullptr, 0);
  Expect(
      status == UGpuMeshPipeline::GpuFinishStatus::Failed,
      "obsolete ticket cannot poll or emit into replacement geometry storage");
  ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache, world, registry);
  glFinish();
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, allocator.GetQuadSsbo());
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, observed.size(),
                     observed.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  Expect(sentinel == observed && allocator.OwnsStagingSlot(replacement) &&
             !cache.HasLiveGpuDraw({0, 0, 0}) &&
             !pipeline->OwnsReadbackSlot(old_ticket),
         "stale compute cleanup preserves replacement bytes and releases its "
         "original PBO");
  std::array<UGpuMeshPipeline::GpuApplyTicket, UGpuMeshPipeline::kReadbackRing>
      tickets;
  for (auto &fresh : tickets)
  {
    fresh = GpuMeshPipelineReadbackTestAccess::Acquire(*pipeline);
    Expect(pipeline->OwnsReadbackSlot(fresh),
           "stale compute cleanup leaves every PBO reusable");
  }
  pipeline->ReleaseReadbackSlot(old_ticket);
  Expect(pipeline->OwnsReadbackSlot(tickets[0]),
         "repeated stale cleanup cannot release fresh readback");
  for (auto &fresh : tickets)
    pipeline->ReleaseReadbackSlot(fresh);
  allocator.ReleaseStagingSlot(replacement);
}

static void TestGpuCapturePolicyTransitions(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  {
    UChunkMeshCache cache;
    cache.SetDeferMeshUntilLitFn([](glm::ivec3) { return true; });
    cache.GetCaptureStore().SetLightInputsPolicy(MeshLightInputs::Baked);
    Expect(cache.ShouldDeferMeshRepairUntilLit({0, 0, 0}),
           "baked repair retains the legacy lighting admission gate");
    cache.GetCaptureStore().SetLightInputsPolicy(MeshLightInputs::PhysicalField);
    Expect(cache.IsDeferMeshUntilLit({0, 0, 0}) &&
               !cache.ShouldDeferMeshRepairUntilLit({0, 0, 0}),
           "physical geometry repair uses publication policy despite raw "
           "column lighting deferral");
  }
  for (const bool kicked : {false, true})
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    world.SetBlock({4, 4, 4}, kStone);
    const glm::ivec3 root(0);
    DeferredTestMesher backend;
    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.AsyncMeshing = true;
    settings.GpuPackedMeshing = true;
    settings.Lighting = LightingMode::Flat;
    world.SetLightingConfigurationIdentity(static_cast<uint64_t>(LightingMode::Flat));
    cache.SetRenderSettings(settings);
    cache.SetMesherBackend(&backend);
    cache.SetMeshRebuildFocus(root, 4, 1);
    cache.MarkDirtyPriority(root);
    const auto snapshot =
        ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, root);
    ChunkMeshCaptureAdmissionTestAccess::QueuePacked(cache, snapshot, registry);
    if (kicked)
    {
      ChunkMeshCaptureAdmissionTestAccess::KickOnly(cache, registry);
      glFinish();
    }
    auto &store = cache.GetCaptureStore();
    store.SetLightInputsPolicy(MeshLightInputs::PhysicalField);
    store.SetLightInputsPolicy(MeshLightInputs::Baked);
    const auto revision = cache.GetChunkMeshRevision(root);
    Expect(bool(store.CaptureAndStore(world, root, revision)),
           "GPU policy fixture captures actual replacement owner");
    ChunkMeshCaptureAdmissionTestAccess::TrackSource(cache, root, revision);
    ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache, world, registry);
    Expect(
        cache.GetPendingGpuAppliesCount() == 0 &&
            cache.GetPublishCount(root) == 0 && !cache.HasLiveGpuDraw(root) &&
            ChunkMeshCaptureAdmissionTestAccess::FreePackedSlots(cache) == 2 &&
            ChunkMeshCaptureAdmissionTestAccess::TracksSource(cache, root,
                                                              revision) &&
            store.TryGet(world, root, revision),
        "queued or computed GPU policy predecessor releases only its own "
        "resources");
    UFlatLightingPipeline flat;
    flat.FillAllLoadedChunks(world);
    const auto *chunk = world.GetChunkManager().GetChunk(root);
    const auto epoch = store.WorldEpoch();
    cache.GetRenderDemandStore().NoteDemand(root, revision, chunk->GetLightFieldRevision(),
        0, 1.0, epoch, chunk->GetIncarnation());
    cache.GetRenderDemandStore().NoteLightCalculationSettled(root, epoch,
        chunk->GetIncarnation(), chunk->GetLightFieldRevision(), chunk->GetContentRevision());
    Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(cache, world, root,
        revision).has_value(), "policy successor captures actual current input");
    const auto tick = cache.RebuildDirtyChunksWithStats(world, registry, 0, 8, false, 0);
    Expect(tick.Scheduled == 1 && cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
           "policy successor real cache worker finishes without fresh MarkDirty");
    cache.DrainAsyncMeshResults(world, registry, 8);
    for (int n = 0; n < 8 && cache.GetPendingGpuAppliesCount(); ++n)
    {
      cache.DrainPendingGpuMeshes(world, registry, 8, 1000);
      glFinish();
    }
    const bool current = cache.IsPublishedMeshCurrentForInputs(root, world) &&
        cache.GetGpuMeshPipeline()->HasGpuMesh(root) && cache.GetPublishCount(root) == 1 &&
        !cache.IsChunkMeshDirty(root) && !cache.IsRemeshAfterApplyPending(root) &&
        cache.GetPendingGpuAppliesCount() == 0;
    Expect(current, "policy predecessor rejection preserves demanded actual GPU successor");
    std::cout << "gpu_policy_successor kicked=" << kicked << " current=" << current << '\n';
    Expect(glGetError() == GL_NO_ERROR,
           "GPU policy transition has no GL errors");
  }
}

static void TestPackedGeometryWaitsForPhysicalLight(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  const glm::ivec3 root(0);
  for (int lifecycle = 0; lifecycle < 14; ++lifecycle)
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    world.SetBlock({4, 4, 4}, kStone);
    if (lifecycle == 10)
      for (int x = -16; x < 32; ++x)
        for (int z = -16; z < 32; ++z)
          world.SetBlock({x, 31, z}, kStone);
    if (lifecycle == 9)
      world.SetBlock({68, 4, 4}, kStone);
    auto *chunk = world.GetChunkManager().GetChunk(root);
    RelightJobSpec spec;
    spec.min_world_y = world.GetVerticalProfile().MinY;
    spec.max_world_y = world.GetVerticalProfile().MaxY;
    if (lifecycle == 10)
    {
      const auto unverified =
          UChunkRelightSnapshot::CaptureSections(world, spec, {root})
              .Compute(registry);
      Expect(unverified.inputs_complete && unverified.chunks.size() == 1,
             "zero-field packed fixture computes complete physical inputs");
      chunk->ReplaceComputedLightVolume(*unverified.chunks[0].padded_light,
                                        true, true);
    }
    const auto captured_light_revision = chunk->GetLightFieldRevision();
    SectionRenderLightBindings resources;
    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.AsyncMeshing = true;
    settings.FrustumCulling = false;
    cache.SetRenderSettings(settings);
    cache.SetMeshRebuildFocus(root, 4, 1);
    const auto geometry = world.CaptureGeometryRenderInputs(
        root, registry.GetDefinitionsCatalogSnapshot());
    const auto epoch = cache.GetCaptureStore().WorldEpoch();
    Expect(resources.BeginWorld(epoch, geometry.world_geometry_identity,
                                lifecycle == 9 ? 1 : 4,
                                4u * SectionLightVolume::CellCount, 4),
           "packed waiting fixture initializes actual physical resources");
    bool reset_during_prepare = false;
    GpuStagingSlotKey replacement_key;
    cache.SetPrepareRenderLightFn(
        [&](uint64_t source_epoch, const SectionGeometryRenderInputs &source,
            const SectionRenderLightSnapshot &light)
        {
          auto binding = resources.Prepare(source_epoch, source, light);
          if (lifecycle >= 12)
          {
            cache.GetCaptureStore().SetLightInputsPolicy(
                MeshLightInputs::PhysicalField);
            cache.GetCaptureStore().SetLightInputsPolicy(
                MeshLightInputs::Baked);
          }
          if (reset_during_prepare)
          {
            auto &slots = cache.GetGpuMeshPipeline()->GetAllocator();
            Expect(slots.Init(2), "callback replaces actual packed storage");
            replacement_key =
                slots.CaptureStagingSlotKey(slots.AllocateStagingSlot(false));
          }
          return lifecycle == 13 ? nullptr : binding;
        });
    cache.MarkDirtyPriority(root);
    auto &demand = cache.GetRenderDemandStore();
    const auto revision = cache.GetChunkMeshRevision(root);
    demand.NoteDemand(root, revision, chunk->GetLightFieldRevision(), 0, 1.0,
                      epoch, chunk->GetIncarnation());
    const auto snapshot =
        ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, root);
    Expect(!snapshot.renderLight && !snapshot.provisionalLightPreview &&
               snapshot.lightInputs == MeshLightInputs::PhysicalField,
           "packed source is light-free geometry before physical calculation "
           "proof");
    ChunkMeshCaptureAdmissionTestAccess::QueuePacked(cache, snapshot, registry);
    for (int tick = 0; tick < 8 && cache.GetPendingGpuAppliesCount(); ++tick)
    {
      ChunkMeshCaptureAdmissionTestAccess::DrainPacked(cache, world, registry);
      glFinish();
    }
    if (cache.GetGeometryAwaitingLightCount() != 1)
      std::cerr << "packed waiting lifecycle=" << lifecycle << '\n';
    Expect(cache.GetLastGpuKickN() == 1 &&
               cache.GetGeometryAwaitingLightCount() == 1 &&
               cache.HasInflightMeshBuild(root) &&
               !cache.HasDrawableGreedyMesh(root) &&
               ChunkMeshCaptureAdmissionTestAccess::FreePackedSlots(cache) == 1,
           "completed packed geometry retains staging ownership while awaiting "
           "light");
    auto &allocator = cache.GetGpuMeshPipeline()->GetAllocator();
    const auto generation = allocator.GetSlotByIndex(0)->generation;
    std::array<PackedQuad, 6> before{}, after{};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, allocator.GetQuadSsbo());
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(before),
                       before.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    switch (lifecycle)
    {
    case 1:
      cache.RemoveChunk(root);
      break;
    case 2:
      world.SetBlock({5, 4, 4}, kStone);
      cache.MarkDirtyPriority(root);
      break;
    case 3:
      cache.GetCaptureStore().BumpWorldEpoch();
      break;
    case 4:
      cache.CancelAsyncMeshWork();
      break;
    case 5:
      allocator.FreeSlotByIndex(0);
      replacement_key =
          allocator.CaptureStagingSlotKey(allocator.AllocateStagingSlot(false));
      break;
    case 6:
      Expect(allocator.Init(2),
             "waiting geometry sees actual allocator reinitialization");
      replacement_key =
          allocator.CaptureStagingSlotKey(allocator.AllocateStagingSlot(false));
      break;
    case 7:
      world.GetChunkManager().RemoveChunk(root);
      world.SetBlock({4, 4, 4}, kStone);
      chunk = world.GetChunkManager().GetChunk(root);
      break;
    case 8:
      reset_during_prepare = true;
      break;
    default:
      break;
    }
    if (lifecycle == 11)
    {
      cache.GetCaptureStore().SetLightInputsPolicy(
          MeshLightInputs::PhysicalField);
      cache.GetCaptureStore().SetLightInputsPolicy(MeshLightInputs::Baked);
    }
    const auto field =
        UChunkRelightSnapshot::CaptureSections(world, spec, {root})
            .Compute(registry);
    Expect(field.inputs_complete && field.chunks.size() == 1,
           "packed waiting fixture computes current physical output");
    chunk->ReplaceComputedLightVolume(*field.chunks[0].padded_light, true,
                                      true);
    Expect(world.RecordLightCalculation(
               field, registry.GetDefinitionsCatalogSnapshot()) == 1,
           "packed waiting fixture installs actual physical proof");
    demand.NoteDemand(root, revision, chunk->GetLightFieldRevision(), 0, 2.0,
                      epoch, chunk->GetIncarnation());
    demand.NoteLightCalculationSettled(root, epoch, chunk->GetIncarnation(),
                                       chunk->GetLightFieldRevision(),
                                       chunk->GetContentRevision());
    std::shared_ptr<SectionRenderLightBinding> blocker;
    if (lifecycle == 9)
    {
      const glm::ivec3 other(4, 0, 0);
      const auto other_field =
          UChunkRelightSnapshot::CaptureSections(world, spec, {other})
              .Compute(registry);
      auto *other_chunk = world.GetChunkManager().GetChunk(other);
      Expect(other_field.inputs_complete && other_field.chunks.size() == 1,
             "metadata pressure fixture computes independent real field");
      other_chunk->ReplaceComputedLightVolume(
          *other_field.chunks[0].padded_light, true, true);
      Expect(world.RecordLightCalculation(
                 other_field, registry.GetDefinitionsCatalogSnapshot()) == 1,
             "metadata pressure fixture installs independent physical proof");
      const auto other_snapshot = world.CaptureRenderLightSnapshot(other);
      Expect(bool(other_snapshot), "metadata pressure has a certified source");
      blocker = resources.Prepare(epoch, other_snapshot->GeometryInputs(),
                                  *other_snapshot);
      Expect(bool(blocker),
             "actual metadata capacity is held by another geometry artifact");
    }
    if (lifecycle == 0)
      cache.DrainPendingGpuMeshes(world, registry, 8, 1000);
    else
      cache.DrainAsyncMeshResults(world, registry, 8);
    if ((lifecycle >= 1 && lifecycle <= 8) || lifecycle >= 11)
    {
      if (cache.GetPublishCount(root) != 0)
        std::cerr << "packed unexpected publication lifecycle=" << lifecycle
                  << '\n';
      Expect(
          cache.GetPublishCount(root) == 0 &&
              (lifecycle < 11 ||
               cache.GetChunkMeshRevision(root) == revision) &&
              cache.GetGeometryAwaitingLightCount() == 0 &&
              !cache.HasLiveGpuDraw(root),
          "cancelled edited superseded or reset GPU geometry cannot publish");
      Expect(replacement_key.IsValid()
                 ? allocator.OwnsStagingSlot(replacement_key) &&
                       allocator.GetFreeSlotCount() == 1
                 : allocator.GetFreeSlotCount() == 2,
             "old GPU cleanup releases its own staging slot and preserves a "
             "replacement");
      if (replacement_key.IsValid())
        allocator.ReleaseStagingSlot(replacement_key);
      continue;
    }
    if (lifecycle == 9)
    {
      Expect(cache.GetPublishCount(root) == 0 &&
                 cache.GetGeometryAwaitingLightCount() == 1 &&
                 cache.GetLastGpuKickN() == 1 &&
                 allocator.GetFreeSlotCount() == 1,
             "real metadata exhaustion retains completed packed geometry "
             "without a retry build");
      blocker.reset();
      resources.CollectRetired();
      glFinish();
      resources.CollectRetired();
      cache.DrainAsyncMeshResults(world, registry, 8);
    }
    const auto *published = allocator.GetSlot(root);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, allocator.GetQuadSsbo());
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(after),
                       after.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    Expect(published && published->generation == generation &&
               published->QuadCount == 6 && cache.HasLiveGpuDraw(root) &&
               cache.GetPublishCount(root) == 1 &&
               cache.GetLastGpuKickN() == 1 && cache.GetLastGpuFinishN() == 1 &&
               cache.GetGeometryAwaitingLightCount() == 0 &&
               std::memcmp(before.data(), after.data(), sizeof(before)) == 0,
           "physical completion publishes original packed slot without compute "
           "resubmission");
    const auto binding = cache.GetPublishedRenderLightBinding(root);
    Expect(binding && binding->IsLive(),
           "late packed publication owns an actual GPU field");
    ProbeCertifiedBinding(resources, binding);
    if (lifecycle == 10)
      Expect(chunk->GetLightFieldRevision() == captured_light_revision &&
                 std::all_of(field.chunks[0].padded_light->cells.begin(),
                             field.chunks[0].padded_light->cells.end(),
                             [](uint8_t cell) { return cell == 0; }),
             "completed zero field admits retained packed geometry without a "
             "data revision bump");
    for (int tick = 0; tick < 4; ++tick)
    {
      const auto stats =
          cache.RebuildDirtyChunksWithStats(world, registry, 8, 1, false, 0);
      Expect(stats.Scheduled == 0 && stats.SyncRebuilt == 0 &&
                 cache.GetPublishCount(root) == 1 && !cache.HasPendingDirty(),
             "late packed physical pairing converges without historical light "
             "remesh");
    }
  }
}
#endif

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

    if (proof_case == ProofCase::Current && !use_gpu_bindings)
    {
      UChunkMeshCache epoch_cache;
      int prepare_calls = 0;
      epoch_cache.SetPrepareRenderLightFn(
          [&](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
              const SectionRenderLightSnapshot &light)
              -> std::shared_ptr<SectionRenderLightBinding>
          {
            ++prepare_calls;
            return std::make_shared<TestRenderLightBinding>(epoch, geometry,
                                                            light);
          });
      auto input = ChunkMeshSnapshot::Capture(world, root, 0);
      input.sourceWorldEpoch = epoch_cache.GetCaptureStore().WorldEpoch();
      epoch_cache.GetCaptureStore().BumpWorldEpoch();
      Expect(!ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(
                 epoch_cache, world, input, registry),
             "old captured epoch cannot borrow current epoch for GPU light "
             "preparation");
      Expect(prepare_calls == 0,
             "stale source epoch is rejected before any resource callback");
      auto current = ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(
          epoch_cache, world, root);
      auto missing = current;
      missing.sourceWorldEpoch = 0;
      Expect(!ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(
                 epoch_cache, world, missing, registry),
             "unowned raw capture cannot acquire current render epoch");
      auto future = current;
      ++future.sourceWorldEpoch;
      Expect(!ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(
                 epoch_cache, world, future, registry) &&
                 prepare_calls == 0,
             "future source epoch cannot allocate renderer resources");
      Expect(ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(
                 epoch_cache, world, current, registry),
             "current capture owner epoch can prepare a compatible binding");
      Expect(
          !epoch_cache.GetCaptureStore().TryCommit(
              root, 0, epoch_cache.GetCaptureStore().WorldEpoch(), input,
              nullptr),
          "capture store cannot relabel old owned snapshot with current epoch");
      auto stored = ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
          epoch_cache, world, root, 0);
      Expect(
          stored && stored->sourceWorldEpoch ==
                        epoch_cache.GetCaptureStore().WorldEpoch(),
          "capture store returns its captured owner epoch in snapshot value");
      epoch_cache.SetPrepareRenderLightFn(
          [&](uint64_t epoch, const SectionGeometryRenderInputs &geometry,
              const SectionRenderLightSnapshot &light)
              -> std::shared_ptr<SectionRenderLightBinding>
          {
            epoch_cache.GetCaptureStore().BumpWorldEpoch();
            return std::make_shared<TestRenderLightBinding>(epoch, geometry,
                                                            light);
          });
      Expect(
          !ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(epoch_cache, world,
                                                            current, registry),
          "resource callback epoch transition cannot publish an old candidate");
      auto worker_input = ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(
          epoch_cache, world, root);
      UAsyncMeshBuilder builder(1);
      Expect(builder.Enqueue(worker_input, registry) &&
                 builder.WaitIdleFor(std::chrono::seconds(5)),
             "actual worker accepts owned geometry snapshot");
      auto completed = builder.DrainCompleted(1);
      Expect(completed.size() == 1 &&
                 completed[0].sourceWorldEpoch ==
                     worker_input.sourceWorldEpoch &&
                 completed[0].sourceWorldEpoch != completed[0].submitEpoch,
             "actual worker carries capture epoch independently of submission "
             "epoch");
      epoch_cache.SetPrepareRenderLightFn({});
      Expect(!ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(
                 epoch_cache, world, input, registry) &&
                 ChunkMeshCaptureAdmissionTestAccess::PrepareOnly(
                     epoch_cache, world, worker_input, registry),
             "capture epoch authority also applies without an optional GPU "
             "callback");
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
    Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(cache, world,
                                                                   root, geom)
               .has_value(),
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
    CountingPhysicalMesher sync_mesher;
    UChunkMeshCache sync_cache;
    auto sync_settings = settings;
    sync_settings.AsyncMeshing = false;
    sync_cache.SetRenderSettings(sync_settings);
    sync_cache.SetMesherBackend(&sync_mesher);
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
    if (use_gpu_bindings)
    {
#if defined(SECTION_LIGHT_BINDING_DRIVER)
      UGreedyGpuBackend backend;
      GreedyGpuPassCache gpu_pass;
      backend.RefreshPassRefs(gpu_pass, cache, cache.GetGreedyOpaqueCutoutRefs(),
                              cache.GetMeshRevision(), cache.GetCullRevision(), 1);
      size_t root_batches = 0;
      for (const auto &gpu_batch : gpu_pass.batches)
        if (gpu_batch.chunkCoord == root)
        {
          ++root_batches;
          Expect(gpu_batch.renderLightBinding == binding,
                 "actual GPU publication carries source CPU artifact light lease");
        }
      Expect(root_batches > 0, "actual GPU publication retains first root mesh");
      backend.DestroyPass(gpu_pass);
      // A drawable uploaded before physical-resource admission was enabled
      // has identical geometry but no lease. A later metadata-only source
      // publication must not reuse that untyped predecessor indefinitely.
      GreedyMeshBatch legacy = *batch;
      legacy.renderLightBinding.reset();
      GreedyGpuPassCache legacy_pass;
      legacy_pass.passId = GreedyGpuPassId::Opaque;
      backend.RefreshPass(legacy_pass, {legacy}, cache.GetMeshRevision(),
                          cache.GetCullRevision(), 1);
      Expect(legacy_pass.batches.size() == 1 &&
                 !legacy_pass.batches[0].renderLightBinding,
             "legacy GPU fixture has real unchanged geometry without field lease");
      std::unordered_set<glm::ivec3, IVec3Hash> drained;
      cache.ConsumeGeometryDirtyChunks(drained);
      cache.BeginGpuPassDirtyFrame();
      const auto cpu_publications = cache.GetPublishCount(root);
      std::vector<GreedyBatchRef> root_refs;
      for (const auto &ref : cache.GetGreedyOpaqueCutoutRefs())
        if (ref.chunkCoord == root)
          root_refs.push_back(ref);
      backend.RefreshPassRefs(legacy_pass, cache, root_refs,
                              cache.GetMeshRevision(), cache.GetCullRevision(), 1);
      Expect(legacy_pass.batches.size() == 1 &&
                 legacy_pass.batches[0].renderLightBinding == binding,
             "GPU reuse cannot omit newly admitted source artifact light lease");
      Expect(cache.GetPublishCount(root) == cpu_publications,
             "field admission repair does not run another CPU geometry publication");
      backend.DestroyPass(legacy_pass);
      backend.RefreshPass(legacy_pass, {legacy}, cache.GetMeshRevision(),
                          cache.GetCullRevision(), 1);
      cache.ConsumeGeometryDirtyChunks(drained);
      cache.BeginGpuPassDirtyFrame();
      backend.RefreshPassRefs(legacy_pass, cache, {}, cache.GetMeshRevision(),
                              cache.GetCullRevision(), 1);
      Expect(legacy_pass.batches.size() == 1 &&
                 legacy_pass.batches[0].renderLightBinding == binding,
             "retained GPU cohorts admit source light outside current visibility refs");
      backend.DestroyPass(legacy_pass);
      backend.RefreshPass(legacy_pass, {legacy}, cache.GetMeshRevision(),
                          cache.GetCullRevision(), 1);
      // Batch indices belong to a source publication, not to the coordinate.
      // The old opaque material occupied index1 in an older material table;
      // its replacement is the current source cohort's actual index0.
      legacy_pass.batches[0].batchIndex = 1;
      backend.RefreshPassRefs(legacy_pass, cache, {}, cache.GetMeshRevision(),
                              cache.GetCullRevision(), 1);
      Expect(legacy_pass.batches.size() == 1 &&
                 legacy_pass.batches[0].batchIndex == 0 &&
                 legacy_pass.batches[0].renderLightBinding == binding,
             "field admission reloads current cohort after material indices change");
      backend.DestroyPass(legacy_pass);
#endif
    }
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
    const auto snapshot =
        ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache, world, root);
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
    Expect(ChunkMeshCaptureAdmissionTestAccess::CaptureForSchedule(
               cache, world, root, cache.GetChunkMeshRevision(root))
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
    cache.RebuildDirtyChunksWithStats(world, registry, 8, 0, false, 0);
    Expect(cache.GetPublishCount(root) > published_count &&
               !cache.IsGeometryAwaitingLight(root),
           "resource recovery publishes retained async geometry without "
           "rescheduling");

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
               sync_cache.IsGeometryAwaitingLight(root) &&
               !sync_cache.IsChunkMeshDirty(root),
           "immediate resource failure retains predecessor and completed "
           "geometry");
    sync_cache.RebuildDirtyChunksWithStats(world, registry, 100, 0, false, 0);
    Expect(sync_cache.GetPublishCount(root) == sync_count &&
               sync_cache.IsGeometryAwaitingLight(root) &&
               !sync_cache.IsChunkMeshDirty(root),
           "bounded synchronous drain retains geometry while resource is "
           "unavailable");
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
              sync_cache.IsGeometryAwaitingLight(root) &&
              !sync_cache.IsChunkMeshDirty(root),
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
      const auto before_field_only_builds = sync_mesher.builds.load();
      const auto before_field_only_root_builds = sync_mesher.BuildsFor(root);
      const auto sync_epoch = sync_cache.GetCaptureStore().WorldEpoch();
      sync_cache.SetRepairGuardWorld(&world);
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
        auto &sync_demand = sync_cache.GetRenderDemandStore();
        sync_demand.NoteDemand(root, sync_cache.GetChunkMeshRevision(root),
                               chunk->GetLightFieldRevision(), 0, 3.0,
                               sync_epoch, chunk->GetIncarnation());
        sync_demand.NoteLightCalculationSettled(
            root, sync_epoch, chunk->GetIncarnation(),
            chunk->GetLightFieldRevision(), chunk->GetContentRevision());
        sync_cache.MarkDirtyPriority(
            root, MeshRevisionBumpReason::PriorityRelitInstallRepair);
        sync_cache.RebuildDirtyChunksWithStats(world, registry, 100, 100, false,
                                               0);
        if (sync_cache.GetPublishedRenderLightBinding(root) !=
                current_binding ||
            !current_binding->Matches(sync_epoch, current_geometry,
                                      *new_light) ||
            sync_cache.GetPublishCount(root) != before_field_only_count ||
            sync_mesher.builds != before_field_only_builds ||
            sync_cache.GetMeshedLightRevision(root) !=
                chunk->GetLightFieldRevision() ||
            sync_cache.GetGreedyVertexCount() != before_field_only_vertices)
          std::cerr << "field-only activation diagnostic emitter="
                    << emitter_present << " same_binding="
                    << (sync_cache.GetPublishedRenderLightBinding(root) ==
                        current_binding)
                    << " binding_matches="
                    << current_binding->Matches(sync_epoch, current_geometry,
                                                *new_light)
                    << " publish=" << sync_cache.GetPublishCount(root) << "/"
                    << before_field_only_count
                    << " builds=" << sync_mesher.builds << "/"
                    << before_field_only_builds
                    << " root_builds=" << sync_mesher.BuildsFor(root) << "/"
                    << before_field_only_root_builds
                    << " emitter_builds=" << sync_mesher.BuildsFor({1, 0, 1})
                    << " light=" << sync_cache.GetMeshedLightRevision(root)
                    << "/" << chunk->GetLightFieldRevision()
                    << " vertices=" << sync_cache.GetGreedyVertexCount() << "/"
                    << before_field_only_vertices
                    << " geom=" << sync_cache.GetChunkMeshRevision(root) << "/"
                    << sync_cache.GetMeshPublishRevs(root).geom_rev
                    << " active=" << sync_cache.HasInflightMeshBuild(root)
                    << " dirty=" << sync_cache.IsChunkMeshDirty(root)
                    << " field_queue="
                    << sync_cache.GetPublishedLightRefreshPendingCount()
                    << '\n';
        Expect(
            sync_cache.GetPublishedRenderLightBinding(root) ==
                    current_binding &&
                current_binding->Matches(sync_epoch, current_geometry,
                                         *new_light) &&
                current_binding->ShaderWords() == current_words &&
                sync_cache.GetPublishCount(root) == before_field_only_count &&
                sync_mesher.BuildsFor(root) == before_field_only_root_builds &&
                sync_cache.GetMeshedLightRevision(root) ==
                    chunk->GetLightFieldRevision() &&
                sync_cache.GetGreedyVertexCount() == before_field_only_vertices,
            "world/cache light-only update preserves binding and geometry "
            "without remeshing");
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

#if defined(SECTION_LIGHT_BINDING_DRIVER)
static void
ProbeCrossGpuHandles(const cutum::CrossGpuBatch &gpu,
                     const std::vector<std::array<uint32_t, 4>> &expected,
                     bool gles)
{
  const std::string version =
      gles ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
           : "#version 330 core\n";
  const auto compile = [&](GLenum type, const std::string &body)
  {
    const auto shader = glCreateShader(type);
    const auto source = version + body;
    const auto *text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    Expect(ok == GL_TRUE, "Cross integer handle probe shader compiles");
    return shader;
  };
  const auto vs = compile(GL_VERTEX_SHADER,
                          "layout(location=6) in uvec4 field; flat out uvec4 "
                          "handle; uniform float width;"
                          "void "
                          "main(){handle=field;gl_Position=vec4(-1.+(float(gl_"
                          "InstanceID)+.5)*2./width,0,0,1);gl_PointSize=1.;}");
  const auto fs =
      compile(GL_FRAGMENT_SHADER, "flat in uvec4 handle; layout(location=0) "
                                  "out uvec4 value;void main(){value=handle;}");
  const auto program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  Expect(ok == GL_TRUE, "Cross handle probe program links");
  GLuint texture = 0, fbo = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32UI,
               static_cast<GLsizei>(expected.size()), 1, 0, GL_RGBA_INTEGER,
               GL_UNSIGNED_INT, nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  Expect(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
         "Cross handle integer framebuffer complete");
  glViewport(0, 0, static_cast<GLsizei>(expected.size()), 1);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glUseProgram(program);
  glUniform1f(glGetUniformLocation(program, "width"), float(expected.size()));
  glBindVertexArray(gpu.vao);
  GLint integer = 0, divisor = 0, stride = 0;
  glGetVertexAttribiv(6, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &integer);
  glGetVertexAttribiv(6, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &divisor);
  glGetVertexAttribiv(6, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
  Expect(integer && divisor == 1 && stride == sizeof(cutum::CrossInstanceGpu),
         "production Cross VAO transports integer words per instance");
  glDrawArraysInstanced(GL_POINTS, 0, 1, static_cast<GLsizei>(expected.size()));
  std::vector<std::array<uint32_t, 4>> actual(expected.size());
  glReadPixels(0, 0, static_cast<GLsizei>(expected.size()), 1, GL_RGBA_INTEGER,
               GL_UNSIGNED_INT, actual.data());
  Expect(actual == expected,
         "actual Cross instanced draw retains all four artifact handle words");
  glBindVertexArray(0);
  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &texture);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  Expect(glGetError() == GL_NO_ERROR, "Cross handle probe has no GL errors");
}
#endif

#if defined(SECTION_LIGHT_BINDING_DRIVER)
static void ProbeCrossPhysicalShader(
    cutum::UCrossGpuBackend &backend, const cutum::CrossGpuBatch &gpu,
    cutum::SectionRenderLightBindings &owner,
    const std::shared_ptr<cutum::SectionRenderLightBinding> &binding,
    size_t index, glm::ivec3 cell, std::array<uint32_t, 4> expected, bool gles,
    bool override_handle = false, bool invalid_handle = false,
    bool physical_light = true)
{
  using namespace cutum;
  std::ifstream file(std::string(CUBATARIUM_SOURCE_DIR) +
                     (gles ? "/shaders/gles/vshader_cross_instanced.glsl"
                           : "/shaders/vshader_cross_instanced.glsl"));
  std::ostringstream contents;
  contents << file.rdbuf();
  Expect(file.good() && !contents.str().empty(),
         "production Cross vertex shader is available");
  const auto compile = [](GLenum type, const std::string &source)
  {
    const auto shader = glCreateShader(type);
    const auto *text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
      char log[4096]{};
      glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
      std::cerr << log << '\n';
    }
    Expect(ok != 0, "production Cross physical shader compiles");
    return shader;
  };
  const auto vs =
      compile(GL_VERTEX_SHADER,
              SectionLightBindingTable::ExpandShaderInterface(contents.str()));
  const std::string version =
      gles ? "#version 300 es\nprecision highp float;precision highp int;\n"
           : "#version 330 core\n";
  const auto fs = compile(
      GL_FRAGMENT_SHADER,
      version + "in float vSkyLight;in float vBlockLight;flat in float "
                "vSectionLightValid;"
                "layout(location=0) out uvec4 value;void main(){value=uvec4("
                "uint(round(vSkyLight*15.)),uint(round(vBlockLight*15.)),"
                "uint(vSectionLightValid),1u);}");
  const auto program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  Expect(ok != 0, "production Cross physical shader links");
  GLuint texture = 0, fbo = 0;
  glGenTextures(1, &texture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32UI, 1, 1, 0, GL_RGBA_INTEGER,
               GL_UNSIGNED_INT, nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  Expect(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
         "Cross physical integer framebuffer is complete");
  const std::array<uint32_t, 4> clear{};
  glClearBufferuiv(GL_COLOR, 0, clear.data());
  glViewport(0, 0, 1, 1);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  glUseProgram(program);
  const glm::mat4 matrix =
      glm::ortho(float(cell.x) - .25f, float(cell.x) + .25f,
                 float(cell.y) - .25f, float(cell.y) + .25f, -100.f, 100.f);
  glUniformMatrix4fv(glGetUniformLocation(program, "mvp_matrix"), 1, GL_FALSE,
                     &matrix[0][0]);
  glUniform1i(glGetUniformLocation(program, "uDecorShape"), 0);
  glUniform1i(glGetUniformLocation(program, "uUseSectionLight"),
              physical_light);
  glUniform1i(glGetUniformLocation(program, "uOverrideSectionLightHandle"),
              override_handle || invalid_handle);
  auto words = owner.PrepareDraw(binding);
  Expect(words && owner.Bind(7, 8),
         "Cross physical range prepares exact field");
  if (invalid_handle)
    *words = {};
  glUniform4uiv(glGetUniformLocation(program, "uSectionLightHandle"), 1,
                words->data());
  const auto incarnation = owner.ShaderIncarnation();
  glUniform2uiv(glGetUniformLocation(program, "uSectionLightTableIncarnation"),
                1, incarnation.data());
  glUniform1i(glGetUniformLocation(program, "uSectionLightAtlas"), 7);
  glUniform1i(glGetUniformLocation(program, "uSectionLightBindings"), 8);
  Expect(backend.DrawRange(gpu, index, 1),
         "actual Cross range draw accepts nonzero first instance");
  std::array<uint32_t, 4> actual{};
  glReadPixels(0, 0, 1, 1, GL_RGBA_INTEGER, GL_UNSIGNED_INT, actual.data());
  if (actual != expected)
    std::cerr << "Cross physical actual=" << actual[0] << ',' << actual[1]
              << ',' << actual[2] << ',' << actual[3]
              << " expected=" << expected[0] << ',' << expected[1] << ','
              << expected[2] << ',' << expected[3] << '\n';
  Expect(actual == expected,
         "production Cross shader samples canonical sky/block and validity");
  glBindVertexArray(gpu.vao);
  void *offset = nullptr;
  glGetVertexAttribPointerv(3, GL_VERTEX_ATTRIB_ARRAY_POINTER, &offset);
  Expect(offset == nullptr, "Cross range restores instance base after draw");
  glBindVertexArray(0);
  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &texture);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  Expect(glGetError() == GL_NO_ERROR, "Cross physical probe has no GL errors");
}
#endif

#if defined(SECTION_LIGHT_BINDING_DRIVER)
static void
TestCrossPassFailureAtomicity(cutum::UBlockRegistry &registry,
                              const cutum::UBlockDefinitionStorage *definitions,
                              bool gles)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  world.SetBlock({4, 4, 4}, kTallGrass);
  world.SetBlock({20, 4, 4}, kSolidCross);
  RelightJobSpec spec;
  spec.min_world_y = 0;
  spec.max_world_y = 15;
  const auto physical = UChunkRelightSnapshot::CaptureSections(
                            world, spec, {{0, 0, 0}, {1, 0, 0}})
                            .Compute(registry);
  Expect(physical.inputs_complete && physical.chunks.size() == 2,
         "Cross atomic fixture computes real physical fields");
  for (const auto &data : physical.chunks)
    world.GetChunkManager().ReplaceComputedSectionLightVolume(
        data.coord, *data.padded_light, true, true);
  Expect(world.RecordLightCalculation(
             physical, registry.GetDefinitionsCatalogSnapshot()) == 2,
         "Cross atomic fixture installs physical proof");
  const std::array<SectionRenderLightSnapshot, 2> fields{
      *world.CaptureRenderLightSnapshot({0, 0, 0}),
      *world.CaptureRenderLightSnapshot({1, 0, 0})};
  SectionRenderLightBindings owner;
  Expect(owner.BeginWorld(
             23, world.GetChunkManager().GetGeometryInputRevisionIdentity(), 16,
             8u * SectionLightVolume::CellCount, 8),
         "Cross atomic physical resource owner initializes");
  using Weak = std::weak_ptr<SectionRenderLightBinding>;
  const auto make_rows = [&](std::vector<Weak> &weak)
  {
    std::vector<CrossInstanceBatch> result;
    weak.clear();
    for (size_t n = 0; n < fields.size(); ++n)
    {
      auto binding = owner.Prepare(23, fields[n].GeometryInputs(), fields[n]);
      Expect(bool(binding),
             "Cross atomic candidate obtains certified physical lease");
      weak.push_back(binding);
      CrossInstanceBatch batch;
      batch.blockId = n ? kSolidCross : kTallGrass;
      CrossInstanceGpu instance;
      instance.center = glm::vec3(float(n * 16 + 4), 4, 4);
      instance.renderLightHandle = binding->ShaderWords();
      AppendCrossInstance(batch, instance, binding);
      result.push_back(std::move(batch));
    }
    return result;
  };
  const auto expired = [](const std::vector<Weak> &weak)
  {
    return std::all_of(weak.begin(), weak.end(),
                       [](const Weak &w) { return w.expired(); });
  };
  const auto all_live = [](const std::vector<Weak> &weak)
  {
    return std::all_of(weak.begin(), weak.end(),
                       [](const Weak &w) { return !w.expired(); });
  };
  const auto words = [](const std::vector<CrossInstanceBatch> &rows)
  {
    std::vector<std::array<uint32_t, 4>> result;
    for (const auto &row : rows)
      result.push_back(row.instances[0].renderLightHandle);
    return result;
  };
  UCrossGpuBackend backend;
  CrossGpuPassCache pass;
  std::vector<Weak> original_weak;
  auto initial = make_rows(original_weak);
  const auto original_words = words(initial);
  Expect(backend.RefreshPass(pass, initial, 1, 1, 1, 1),
         "Cross atomic initial pass publishes");
  initial.clear();
  const std::array<GLuint, 2> original_buffers{pass.batches[0].instanceVbo,
                                               pass.batches[1].instanceVbo};
  const std::array<GLuint, 2> original_vaos{pass.batches[0].vao,
                                            pass.batches[1].vao};
  const auto unchanged = [&]()
  {
    Expect(pass.batches.size() == 2 && pass.meshRevision == 1 &&
               pass.cullRevision == 1 &&
               pass.batches[0].instanceVbo == original_buffers[0] &&
               pass.batches[1].instanceVbo == original_buffers[1] &&
               pass.batches[0].vao == original_vaos[0] &&
               pass.batches[1].vao == original_vaos[1] &&
               all_live(original_weak),
           "failed Cross publication retains complete predecessor buffers "
           "leases and revisions");
    for (size_t n = 0; n < 2; ++n)
      ProbeCrossGpuHandles(pass.batches[n], {original_words[n]}, gles);
  };
  std::vector<Weak> rejected_weak;
  auto rejected = make_rows(rejected_weak);
  glBindBuffer(GL_TEXTURE_2D, 0);
  Expect(!backend.RefreshPass(pass, rejected, 2, 2, 2, 2),
         "Cross caller GL error rejects before publication");
  unchanged();
  CrossGpuBackendDriverTestAccess::Fault late{2};
  CrossGpuBackendDriverTestAccess::Arm(backend, late);
  Expect(!backend.RefreshPass(pass, rejected, 2, 2, 2, 2) && late.calls == 2,
         "actual second Cross transfer error rejects whole staged pass");
  CrossGpuBackendDriverTestAccess::Clear(backend);
  rejected.clear();
  Expect(expired(rejected_weak),
         "failed staged Cross uploads release every candidate field lease");
  unchanged();
  Expect(glGetError() == GL_NO_ERROR,
         "handled Cross transfer failure leaves no latent GL error");
  auto invalid_shape = make_rows(rejected_weak);
  invalid_shape[1].shape = static_cast<BlockDecorShape>(255);
  Expect(!backend.RefreshPass(pass, invalid_shape, 2, 2, 2, 2),
         "invalid later Cross shape cannot publish a valid prefix");
  invalid_shape.clear();
  Expect(expired(rejected_weak),
         "invalid shape rollback releases staged field leases");
  unchanged();
  std::vector<Weak> committed_weak;
  auto recovered = make_rows(committed_weak);
  auto expected = words(recovered);
  Expect(backend.RefreshPass(pass, recovered, 2, 2, 2, 2),
         "same desired Cross revision retries successfully after transfer "
         "failure");
  recovered.clear();
  Expect(expired(original_weak) && all_live(committed_weak),
         "atomic swap releases predecessor ownership and keeps only committed "
         "fields");
  for (size_t n = 0; n < 2; ++n)
    ProbeCrossGpuHandles(pass.batches[n], {expected[n]}, gles);
  Expect(CrossGpuBackendDriverTestAccess::Fence(backend) != nullptr,
         "retired Cross scratch owns a fence before reuse");
  std::set<GLuint> buffer_names(original_buffers.begin(),
                                original_buffers.end());
  for (const auto &batch : pass.batches)
    buffer_names.insert(batch.instanceVbo);
  Expect(!backend.RefreshPass(pass, {}, 2, 2, 3, 3),
         "Cross same-revision shortcut cannot bypass live desire validation");
  for (uint64_t revision = 3; revision <= 6; ++revision)
  {
    std::vector<Weak> next_weak;
    auto next = make_rows(next_weak);
    expected = words(next);
    Expect(
        backend.RefreshPass(pass, next, revision, revision, revision, revision),
        "Cross scratch storage publishes repeated successors");
    next.clear();
    Expect(expired(committed_weak),
           "scratch buffers do not retain obsolete physical ownership");
    committed_weak = std::move(next_weak);
    for (size_t n = 0; n < 2; ++n)
    {
      buffer_names.insert(pass.batches[n].instanceVbo);
      ProbeCrossGpuHandles(pass.batches[n], {expected[n]}, gles);
    }
  }
  Expect(buffer_names.size() == 4,
         "two Cross batches reuse only two complete sets of instance buffers");
  const auto retired_fence =
      static_cast<GLsync>(CrossGpuBackendDriverTestAccess::Fence(backend));
  Expect(retired_fence && glIsSync(retired_fence),
         "Cross scratch fence remains owned until reuse or shutdown");
  // Keep page owners alive during the sync query. Their final release can
  // create atlas retirement fences and immediately reuse the deleted address.
  std::vector<std::shared_ptr<SectionRenderLightBinding>> closing_leases;
  for (const auto &batch : pass.batches)
    for (const auto &range : batch.lightRanges)
      closing_leases.push_back(range.binding);
  backend.DestroyAll(pass);
  Expect(!CrossGpuBackendDriverTestAccess::Fence(backend) &&
             !glIsSync(retired_fence),
         "Cross shutdown deletes its scratch fence before releasing page owners");
  closing_leases.clear();
  Expect(expired(committed_weak),
         "Cross shutdown releases committed and scratch field leases");
  for (auto buffer : buffer_names)
    Expect(!glIsBuffer(buffer),
           "Cross shutdown deletes active and scratch instance storage");

  UCrossGpuBackend empty_backend;
  CrossGpuPassCache empty;
  std::vector<Weak> first_weak;
  auto first = make_rows(first_weak);
  CrossGpuBackendDriverTestAccess::Fault first_fault{2};
  CrossGpuBackendDriverTestAccess::Arm(empty_backend, first_fault);
  Expect(
      !empty_backend.RefreshPass(empty, first, 1, 1, 1, 1) &&
          empty.batches.empty() && empty.meshRevision == 0 &&
          empty.cullRevision == 0,
      "failed first Cross publication cannot expose a partially uploaded pass");
  CrossGpuBackendDriverTestAccess::Clear(empty_backend);
  Expect(empty_backend.RefreshPass(empty, first, 1, 1, 1, 1),
         "first Cross publication recovers without changing desired revision");
  first.clear();
  empty_backend.DestroyAll(empty);
  Expect(expired(first_weak) && glGetError() == GL_NO_ERROR,
         "initial Cross failure recovery retires all resources cleanly");

  auto selection_rows = make_rows(first_weak);
  Expect(empty_backend.RefreshPass(empty, selection_rows, 7, 9, 7, 9, {1, 1}),
         "Cross selection initially publishes both ready sections");
  const auto second_words = selection_rows[1].instances[0].renderLightHandle;
  Expect(empty_backend.RefreshPass(empty, {selection_rows[1]}, 7, 9, 7, 9,
                                   {0, 1}) &&
             empty.batches.size() == 1 &&
             empty.instanceSelection == std::vector<uint8_t>({0, 1}) &&
             empty.meshRevision == 7 && empty.cullRevision == 9,
         "readiness loss updates actual Cross selection at unchanged mesh and "
         "cull revisions");
  ProbeCrossGpuHandles(empty.batches[0], {second_words}, gles);
  const auto selected_buffer = empty.batches[0].instanceVbo;
  CrossGpuBackendDriverTestAccess::Fault selection_fault{1};
  CrossGpuBackendDriverTestAccess::Arm(empty_backend, selection_fault);
  Expect(empty_backend.RefreshPass(empty, {selection_rows[1]}, 7, 9, 7, 9,
                                   {0, 1}) &&
             selection_fault.calls == 0 &&
             empty.batches[0].instanceVbo == selected_buffer,
         "unchanged exact readiness selection reuses its GPU buffers");
  Expect(!empty_backend.RefreshPass(empty, {selection_rows[0]}, 7, 9, 7, 9,
                                    {1, 0}) &&
             selection_fault.calls == 1 &&
             empty.batches[0].instanceVbo == selected_buffer &&
             empty.instanceSelection == std::vector<uint8_t>({0, 1}),
         "failed Cross readiness replacement retains previous selection "
         "identity atomically for retry");
  CrossGpuBackendDriverTestAccess::Clear(empty_backend);
  ProbeCrossGpuHandles(empty.batches[0], {second_words}, gles);
  const auto first_words = selection_rows[0].instances[0].renderLightHandle;
  Expect(empty_backend.RefreshPass(empty, {selection_rows[0]}, 7, 9, 7, 9,
                                   {1, 0}) &&
             empty.instanceSelection == std::vector<uint8_t>({1, 0}),
         "Cross readiness recovers without inventing a geometry revision");
  ProbeCrossGpuHandles(empty.batches[0], {first_words}, gles);
  selection_rows.clear();
  empty_backend.DestroyAll(empty);
  Expect(expired(first_weak) && empty.instanceSelection.empty() &&
             glGetError() == GL_NO_ERROR,
         "Cross selection shutdown releases exact source ownership and mask");
}
#endif

static void TestCrossLightArtifactTransport(
    cutum::UBlockRegistry &registry,
    const cutum::UBlockDefinitionStorage *definitions, bool gpu = false,
    bool gles = false)
{
  using namespace cutum;
  SectionRenderLightBindings owner;
#if defined(SECTION_LIGHT_BINDING_DRIVER)
  UCrossGpuBackend backend;
  CrossGpuPassCache pass;
#endif
  std::weak_ptr<SectionRenderLightBinding> weak_first, weak_second;
  std::vector<std::array<uint32_t, 4>> expected;
  {
    UBlockWorld world;
    world.SetFluidDefinitions(definitions);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    for (int x : {4, 5, 20, 21})
      world.SetBlock({x, 4, 4}, kTallGrass);
    world.SetBlock({15, 8, 15}, kTallGrass);
    RelightJobSpec spec;
    spec.min_world_y = 0;
    spec.max_world_y = 15;
    const auto physical = UChunkRelightSnapshot::CaptureSections(
                              world, spec, {{0, 0, 0}, {1, 0, 0}})
                              .Compute(registry);
    Expect(physical.inputs_complete && physical.chunks.size() == 2,
           "Cross fixture computes two real physical fields");
    for (const auto &data : physical.chunks)
      world.GetChunkManager().ReplaceComputedSectionLightVolume(
          data.coord, *data.padded_light, true, true);
    Expect(world.RecordLightCalculation(
               physical, registry.GetDefinitionsCatalogSnapshot()) == 2,
           "Cross fixture installs physical field provenance");
    UChunkMeshCache cache;
    auto settings = MakeFrustumMeshSettings();
    settings.FrustumCulling = false;
    settings.AsyncMeshing = false;
    cache.SetRenderSettings(settings);
    const auto epoch = cache.GetCaptureStore().WorldEpoch();
    cache.SetPrepareRenderLightFn(
        [&](uint64_t source, const SectionGeometryRenderInputs &geometry,
            const SectionRenderLightSnapshot &light)
            -> std::shared_ptr<SectionRenderLightBinding>
        {
          if (gpu)
            return owner.PrepareForWorld(source, geometry, light);
          return std::make_shared<TestRenderLightBinding>(source, geometry,
                                                          light);
        });
    cache.RebuildChunkImmediate(world, registry, {0, 0, 0});
    cache.RebuildChunkImmediate(world, registry, {1, 0, 0});
    cache.UpdateVisibleInstances(Frustum{}, glm::mat4(1), glm::vec3(0));
    auto first = cache.GetPublishedRenderLightBinding({0, 0, 0});
    auto second = cache.GetPublishedRenderLightBinding({1, 0, 0});
    Expect(first && second && first != second,
           "Cross section artifacts own distinct physical leases");
    weak_first = first;
    weak_second = second;
    auto flattened = cache.GetCrossBatches();
    Expect(flattened.size() == 1 && flattened[0].instances.size() == 5 &&
               flattened[0].lightRanges.size() == 2,
           "actual cache flatten retains two source section ranges");
    auto filtered =
        FilterCrossInstances(flattened[0], [](const CrossInstanceGpu &i)
                             { return i.center.x >= 5.0f; });
    Expect(filtered.instances.size() == 4 && filtered.lightRanges.size() == 2,
           "actual Cross filter compacts instances and both source ranges");
    size_t covered = 0;
    for (const auto &range : filtered.lightRanges)
    {
      Expect(range.first == covered && range.count > 0 && range.binding,
             "filtered ranges cover each retained instance exactly once");
      for (size_t n = range.first; n < range.first + range.count; ++n)
      {
        const auto &i = filtered.instances[n];
        const auto binding = i.center.x < 16 ? first : second;
        Expect(range.binding == binding &&
                   i.renderLightHandle == binding->ShaderWords(),
               "flatten/filter words and lease belong to the exact original "
               "artifact");
        expected.push_back(i.renderLightHandle);
      }
      covered += range.count;
    }
    Expect(covered == filtered.instances.size(),
           "filtered lease ranges cover whole GPU input");
    if (gpu)
    {
#if defined(SECTION_LIGHT_BINDING_DRIVER)
      Expect(first->ShaderWords() != second->ShaderWords(),
             "actual GPU owner gives source sections distinguishable handles");
      backend.RefreshPass(pass, {filtered}, 1, 1, 1, 1);
      Expect(pass.batches.size() == 1 &&
                 pass.batches[0].lightRanges.size() == 2,
             "production Cross GPU pass owns both field leases");
      ProbeCrossGpuHandles(pass.batches[0], expected, gles);
      ProbeCrossPhysicalShader(backend, pass.batches[0], owner, first, 1,
                               {15, 8, 15}, {15, 0, 1, 1}, gles);
      ProbeCrossPhysicalShader(backend, pass.batches[0], owner, first, 1,
                               {15, 8, 15}, {0, 0, 0, 1}, gles, true, true);
      const auto buffer = pass.batches[0].instanceVbo;
      const auto words = first->ShaderWords();
      world.SetBlock({16, 8, 16}, kFire);
      const auto changed =
          UChunkRelightSnapshot::CaptureSections(world, spec, {{0, 0, 0}})
              .Compute(registry);
      Expect(changed.inputs_complete && changed.chunks.size() == 1,
             "Cross field-only successor has complete real transport inputs");
      world.GetChunkManager().ReplaceComputedSectionLightVolume(
          {0, 0, 0}, *changed.chunks[0].padded_light, true, true);
      Expect(world.RecordLightCalculation(
                 changed, registry.GetDefinitionsCatalogSnapshot()) == 1,
             "Cross field successor installs canonical proof");
      const auto successor = world.CaptureRenderLightSnapshot({0, 0, 0});
      Expect(
          successor && successor->MatchesGeometry(first->GeometryInputs()) &&
              UnpackBlock(successor->Volume()
                              ->cells[SectionLightVolume::Index(15, 8, 15)]) ==
                  12 &&
              owner.Refresh(first, epoch, *successor) &&
              first->ShaderWords() == words,
          "Cross source changes physical light with the same geometry handle");
      ProbeCrossPhysicalShader(backend, pass.batches[0], owner, first, 1,
                               {15, 8, 15}, {15, 12, 1, 1}, gles);
      SectionRenderLightBindings fallback;
      Expect(fallback.BeginWorld(
                 epoch, first->GeometryInputs().world_geometry_identity, 1,
                 SectionLightVolume::CellCount, 1, true),
             "minimal Cross fallback owner initializes");
      auto fallback_binding =
          fallback.Prepare(epoch, first->GeometryInputs(), *successor);
      Expect(fallback_binding && !fallback.IsResident(fallback_binding),
             "minimal Cross owner requires ordered fallback");
      ProbeCrossPhysicalShader(backend, pass.batches[0], fallback,
                               fallback_binding, 1, {15, 8, 15}, {15, 12, 1, 1},
                               gles, true);
      glBindBuffer(GL_ARRAY_BUFFER, buffer);
      const auto bytes = filtered.instances.size() * sizeof(CrossInstanceGpu);
      const auto *mapped = glMapBufferRange(
          GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), GL_MAP_READ_BIT);
      Expect(mapped &&
                 std::memcmp(mapped, filtered.instances.data(), bytes) == 0,
             "physical Cross light refresh leaves every actual instance byte "
             "unchanged");
      Expect(glUnmapBuffer(GL_ARRAY_BUFFER) == GL_TRUE &&
                 pass.batches[0].instanceVbo == buffer &&
                 pass.meshRevision == 1 && pass.cullRevision == 1,
             "Cross field refresh preserves geometry buffer and publication "
             "revisions");
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      ProbeCrossGpuHandles(pass.batches[0], expected, gles);
#endif
    }
  }
  if (gpu)
  {
#if defined(SECTION_LIGHT_BINDING_DRIVER)
    Expect(!weak_first.expired() && !weak_second.expired(),
           "GPU Cross pass retains source fields after CPU geometry cache and "
           "ranges are destroyed");
    ProbeCrossGpuHandles(pass.batches[0], expected, gles);
    backend.DestroyAll(pass);
    Expect(weak_first.expired() && weak_second.expired(),
           "GPU pass destruction releases all Cross field leases");
#endif
  }
  else
    Expect(weak_first.expired() && weak_second.expired(),
           "CPU range owners retire with their geometry artifacts");
}

#if defined(SECTION_LIGHT_BINDING_DRIVER)
static void TestMdiLightDrawTable()
{
  using namespace cutum;
  SectionLightDrawTable table;
  std::vector<SectionLightDrawTable::Words> rows{{0x10000001u, 17, 99, 201},
                                                 {0x80000002u, 29, 109, 211},
                                                 {0xf0000003u, 31, 119, 221}};
  const auto original = rows;
  UShaderProgram program;
  std::ifstream file(std::string(CUBATARIUM_SOURCE_DIR) +
                     "/shaders/vshader_greedy.glsl");
  std::ostringstream vertex;
  vertex << file.rdbuf();
  Expect(program.CreateFromStrings(
             vertex.str(),
             "#version 330 core\nflat in uvec4 vSurfaceRenderLightHandle;"
             "layout(location=0) out uvec4 value;"
             "void main(){value=vSurfaceRenderLightHandle;}"),
         "production greedy vertex transports integer MDI light handles");
  GLuint vao = 0, vbo = 0, ebo = 0, color = 0, fbo = 0;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);
  std::vector<GreedyMeshVertex> vertices(18);
  std::vector<uint32_t> indices(18);
  const int corners[] = {0, 1, 2, 0, 2, 3};
  GreedyGpuPassCache cache;
  cache.usesVertexPool = true;
  cache.poolVbo = vbo;
  cache.poolEbo = ebo;
  cache.batches.resize(3);
  for (int n = 0; n < 3; ++n)
  {
    for (int k = 0; k < 6; ++k)
    {
      auto &v = vertices[n * 6 + k];
      v.px =
          -1.f + (n + ((corners[k] == 1 || corners[k] == 2) ? 1 : 0)) * 2.f / 3;
      v.py = corners[k] >= 2 ? 1.f : -1.f;
      indices[n * 6 + k] = k;
    }
    auto &gpu = cache.batches[n];
    gpu.pooled = true;
    gpu.vertexCount = gpu.indexCount = gpu.indexCountGl = 6;
    gpu.vboByteOffset = n * 6 * sizeof(GreedyMeshVertex);
    gpu.eboByteOffset = n * 6 * sizeof(uint32_t);
  }
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GreedyMeshVertex),
               vertices.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GreedyMeshVertex),
                        nullptr);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
               indices.data(), GL_STATIC_DRAW);
  glGenTextures(1, &color);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, color);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32UI, 3, 1, 0, GL_RGBA_INTEGER,
               GL_UNSIGNED_INT, nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         color, 0);
  Expect(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
         "MDI light metadata integer target is complete");
  glViewport(0, 0, 3, 1);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  program.Use();
  program.SetMat4("mvp_matrix", glm::mat4(1));
  UMdiVertexPoolStore store;
  UMdiVertexPoolStore legacy_store(false);
  Expect(!legacy_store.SupportsDrawLightRows(),
         "legacy indirect capability disables base-instance light rows");
  std::vector<DrawElementsIndirectCommand> legacy_commands;
  Expect(legacy_store.BuildIndirectCommandsRange(cache, 0, 3,
                                                 legacy_commands) == 3 &&
             std::all_of(legacy_commands.begin(), legacy_commands.end(),
                         [](const auto &command)
                         { return command.baseInstance == 0; }),
         "legacy indirect producer keeps the reserved command word zero");
  MdiVertexPoolStoreDriverTestAccess::Rebuild(legacy_store, cache);
  std::array<DrawElementsIndirectCommand, 3> legacy_gpu_commands{};
  glBindBuffer(GL_COPY_READ_BUFFER, cache.IndirectCmdsBuffer);
  glGetBufferSubData(GL_COPY_READ_BUFFER, 0, sizeof(legacy_gpu_commands),
                     legacy_gpu_commands.data());
  Expect(!cache.IndirectUsesLightRows &&
             std::all_of(legacy_gpu_commands.begin(), legacy_gpu_commands.end(),
                         [](const auto &command)
                         { return command.baseInstance == 0; }),
         "legacy GPU command table keeps reserved base-instance words zero");
  glBindBuffer(GL_COPY_READ_BUFFER, 0);
  const auto draw = [&](bool gpu_commands, bool hidden_middle = false)
  {
    Expect(table.BindAttribute(), "MDI exact metadata attribute binds");
    const uint32_t clear[] = {0, 0, 0, 0};
    glClearBufferuiv(GL_COLOR, 0, clear);
    if (gpu_commands)
    {
      MdiVertexPoolStoreDriverTestAccess::Rebuild(store, cache);
      Expect(store.SubmitIndirectCommandsGpuRange(cache, 0, 3),
             "actual GPU-resident command table submits light-indexed MDI");
    }
    else
    {
      std::vector<DrawElementsIndirectCommand> commands;
      const auto count =
          store.BuildIndirectCommandsRange(cache, 0, 3, commands);
      Expect(count == (hidden_middle ? 2u : 3u),
             "actual CPU indirect builder retains visible command count");
      Expect(store.SubmitIndirectCommands(commands),
             "actual CPU-built indirect commands submit light-indexed MDI");
    }
    table.SignalDrawComplete();
    std::array<SectionLightDrawTable::Words, 3> actual{};
    glReadPixels(0, 0, 3, 1, GL_RGBA_INTEGER, GL_UNSIGNED_INT, actual.data());
    for (int n = 0; n < 3; ++n)
      Expect(
          actual[n] == (hidden_middle && n == 1 ? SectionLightDrawTable::Words{}
                                                : original[n]),
          "MDI light words match exact artifact after filtering and reorder");
    Expect(glGetError() == GL_NO_ERROR,
           "MDI light metadata draw has no GL errors");
  };
  Expect(table.Prepare(rows), "MDI initial metadata uploads");
  const auto uploads = table.UploadCount();
  Expect(table.Prepare(rows) && table.UploadCount() == uploads,
         "unchanged MDI light handles do not upload any buffer");
  draw(false);
  draw(true);
  std::swap(rows[0], rows[2]);
  std::swap(cache.batches[0], cache.batches[2]);
  Expect(table.Prepare(rows), "MDI row reorder publishes matching metadata");
  draw(false);
  draw(true);
  cache.batches[1].drawInstanceCount = 0;
  draw(false, true);
  cache.batches[1].drawInstanceCount = 1;
  const auto buffer = table.Buffer();
  const auto before_failure = table.UploadCount();
  auto wrong_rows = rows;
  wrong_rows[0][0] ^= 1u;
  SectionLightDrawTableTestAccess::FailTransfer(table, true);
  Expect(!table.Prepare(wrong_rows),
         "actual failed metadata transfer is rejected");
  SectionLightDrawTableTestAccess::FailTransfer(table, false);
  Expect(table.Buffer() == buffer && table.UploadCount() == before_failure,
         "failed MDI update keeps predecessor storage and publication");
  draw(false);
  Expect(table.Prepare(rows),
         "matching predecessor remains reusable after failure");
  // Destruction is context-owned, with the same buffers and shaders still live.
  table.Reset();
  Expect(table.Buffer() == 0, "MDI light table resets all context resources");
  UGreedyGpuBackend backend;
  backend.DestroyPass(cache);
  glBindVertexArray(vao);
  SectionLightDrawTable::DisableAttribute();
  program.Unuse();
  glBindVertexArray(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &color);
  glDeleteBuffers(1, &vbo);
  glDeleteBuffers(1, &ebo);
  glDeleteVertexArrays(1, &vao);
}

static void
TestSurfacePhysicalField(cutum::UBlockRegistry &registry,
                         const cutum::UBlockDefinitionStorage *definitions,
                         bool gles, bool packed, bool distant = false,
                         bool mdi = false)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  const glm::ivec3 root =
      distant ? glm::ivec3(-2049, -1, -2050) : glm::ivec3(-1);
  const glm::ivec3 origin = root * 16, center = origin + glm::ivec3(8);
  const glm::ivec3 emitter = origin + glm::ivec3(16, 8, 16);
  world.SetBlock(origin + glm::ivec3(8, 1, 8), kTallGrass);
  RelightJobSpec spec;
  spec.min_world_y = world.GetVerticalProfile().MinY;
  spec.max_world_y = world.GetVerticalProfile().MaxY;
  const auto calculate = [&]()
  {
    const auto result =
        UChunkRelightSnapshot::CaptureSections(world, spec, {root})
            .Compute(registry);
    Expect(result.inputs_complete && result.chunks.size() == 1,
           "surface fixture computes complete physical field");
    world.GetChunkManager().ReplaceComputedSectionLightVolume(
        root, *result.chunks[0].padded_light, true, true);
    Expect(world.RecordLightCalculation(
               result, registry.GetDefinitionsCatalogSnapshot()) == 1,
           "surface fixture records real calculation proof");
    return *world.CaptureRenderLightSnapshot(root);
  };
  auto field = calculate();
  SectionRenderLightBindings owner;
  Expect(owner.BeginWorld(19, field.GeometryInputs().world_geometry_identity),
         "surface physical GPU owner initializes");
  auto binding = owner.Prepare(19, field.GeometryInputs(), field);
  auto *draw_owner = &owner;
  Expect(binding && owner.IsResident(binding), "surface field is resident");
  const std::string directory = std::string(CUBATARIUM_SOURCE_DIR) +
                                (gles ? "/shaders/gles/" : "/shaders/");
  UShaderProgram program;
  Expect(
      program.CreateFromFiles(directory + (packed ? "vshader_packed_greedy.glsl"
                                                  : "vshader_greedy.glsl"),
                              directory + "fshader_greedy.glsl"),
      "production surface vertex/fragment pair links");
  GLuint vao = 0, vbo = 0, texture = 0, color = 0, fbo = 0, quads = 0, ebo = 0;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  GreedyGpuPassCache mdi_cache;
  UMdiVertexPoolStore mdi_store;
  UMdiVertexPoolStore legacy_store(false);
  SectionLightDrawTable draw_table;
  if (mdi)
  {
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    const uint32_t indices[] = {0, 1, 2, 3, 4, 5};
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                 GL_STATIC_DRAW);
    mdi_cache.usesVertexPool = true;
    mdi_cache.poolVbo = vbo;
    mdi_cache.poolEbo = ebo;
    mdi_cache.batches.resize(2);
    auto &batch = mdi_cache.batches[1];
    batch.pooled = true;
    batch.vertexCount = batch.indexCount = batch.indexCountGl = 6;
  }
  glGenTextures(1, &texture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  const std::array<uint8_t, 4> white{255, 255, 255, 255};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               white.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenTextures(1, &color);
  glBindTexture(GL_TEXTURE_2D, color);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         color, 0);
  Expect(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
         "surface reference framebuffer is complete");
  if (packed)
    glGenBuffers(1, &quads);
  glViewport(0, 0, 16, 1);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  program.Use();
  program.SetInt("texture0", 0);
  // The actual production fragment shader emits the canonical two channels
  // through its normal light-debug output; no test replacement fragment code.
  program.SetFloat("uEnvLightDebugMode", 1);
  program.SetFloat("uLightPreviewOverride", 1);
  program.SetFloat("uLightPreviewFade", 1);
  program.SetFloat("uFluidAlpha", 1);
  const auto draw = [&](int face, int phase, bool physical, bool invalid)
  {
    const int normal = face == 0 || face == 2   ? 2
                       : face == 1 || face == 3 ? 0
                                                : 1;
    const int u = (normal + 1) % 3;
    const int v = (normal + 2) % 3;
    const bool negative = face == 2 || face == 3 || face == 5;
    glm::vec3 pos(center);
    pos[normal] += negative ? .5f : -.5f;
    glm::mat4 matrix(0.f);
    matrix[u][0] = 2.f / 16;
    matrix[v][1] = 2.f;
    matrix[3][0] = -(float(origin[u]) + 7.5f) * 2.f / 16;
    matrix[3][1] = -float(center[v]) * 2.f;
    matrix[3][3] = 1.f;
    program.SetMat4("mvp_matrix", matrix);
    if (packed)
    {
      glm::ivec3 local(8);
      local[u] = 0;
      local[normal] = negative ? 9 : 7;
      const uint32_t words[] = {uint32_t(local.x) | (uint32_t(local.y) << 5) |
                                    (uint32_t(local.z) << 10) | (16u << 15) |
                                    (1u << 20) | (uint32_t(face) << 25),
                                (15u << 10) | (15u << 14)};
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, quads);
      glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(words), words,
                   GL_DYNAMIC_DRAW);
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, quads);
      program.SetIVec3("chunkOrigin", origin);
    }
    else
    {
      const int corners[] = {0, 1, 2, 0, 2, 3};
      std::array<GreedyMeshVertex, 6> vertices{};
      for (int n = 0; n < 6; ++n)
      {
        auto p = pos;
        p[u] = float(origin[u]) +
               (corners[n] == 1 || corners[n] == 2 ? 15.5f : -.5f);
        p[v] += corners[n] >= 2 ? .5f : -.5f;
        auto &vertex = vertices[n];
        vertex.px = p.x;
        vertex.py = p.y;
        vertex.pz = p.z;
        vertex.faceIndex = float(face);
        vertex.skyLight = vertex.blockLight =
            1; // deliberately wrong baked data
        vertex.lightPreview = 1;
      }
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(),
                   GL_DYNAMIC_DRAW);
      const auto attrib = [](GLuint index, GLint size, size_t offset)
      {
        glVertexAttribPointer(index, size, GL_FLOAT, GL_FALSE,
                              sizeof(GreedyMeshVertex),
                              reinterpret_cast<void *>(offset));
        glEnableVertexAttribArray(index);
      };
      attrib(0, 3, 0);
      attrib(1, 1, offsetof(GreedyMeshVertex, faceIndex));
      attrib(2, 2, offsetof(GreedyMeshVertex, u));
      attrib(3, 1, offsetof(GreedyMeshVertex, skyLight));
      attrib(4, 1, offsetof(GreedyMeshVertex, blockLight));
      attrib(5, 1, offsetof(GreedyMeshVertex, wetness));
      if (!gles)
        attrib(6, 1, offsetof(GreedyMeshVertex, lightPreview));
    }
    auto handle = draw_owner->PrepareDraw(binding);
    Expect(handle && draw_owner->Bind(7, 8),
           "surface exact field prepares for draw");
    if (invalid)
      *handle = {};
    glUniform4uiv(program.GetUniformLocation("uSurfaceSectionLightHandle"), 1,
                  handle->data());
    const auto incarnation = draw_owner->ShaderIncarnation();
    glUniform2uiv(program.GetUniformLocation("uSectionLightTableIncarnation"),
                  1, incarnation.data());
    program.SetBool("uUseSurfaceSectionLight", physical);
    if (mdi)
    {
      Expect(draw_table.Prepare({{}, *handle}) && draw_table.BindAttribute(),
             "physical surface MDI table prepares nonzero source row");
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    for (unsigned submit = 0; submit < (mdi ? 4u : 1u); ++submit)
    {
      program.SetBool("uUseSurfaceSectionLightDrawTable", mdi && submit < 2);
      const auto uniform_handle =
          mdi && submit < 2 ? SectionLightDrawTable::Words{} : *handle;
      glUniform4uiv(program.GetUniformLocation("uSurfaceSectionLightHandle"), 1,
                    uniform_handle.data());
      const float clear[] = {0, 0, 0, 0};
      glClearBufferfv(GL_COLOR, 0, clear);
      if (!mdi)
        glDrawArrays(GL_TRIANGLES, 0, 6);
      else if (submit == 0)
      {
        std::vector<DrawElementsIndirectCommand> commands;
        Expect(mdi_store.BuildIndirectCommandsRange(mdi_cache, 1, 2,
                                                    commands) == 1 &&
                   mdi_store.SubmitIndirectCommands(commands),
               "physical surface uses production CPU indirect command at row1");
      }
      else if (submit == 1)
      {
        MdiVertexPoolStoreDriverTestAccess::Rebuild(mdi_store, mdi_cache);
        Expect(mdi_store.SubmitIndirectCommandsGpuRange(mdi_cache, 1, 2),
               "physical surface uses production GPU-resident command at row1");
      }
      else if (submit == 2)
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
      else
      {
        std::vector<DrawElementsIndirectCommand> commands;
        Expect(legacy_store.BuildIndirectCommandsRange(mdi_cache, 1, 2,
                                                       commands) == 1 &&
                   commands[0].baseInstance == 0 &&
                   legacy_store.SubmitIndirectCommands(commands),
               "legacy indirect surface draws exact uniform field with "
               "reserved zero");
      }
      std::array<uint8_t, 64> actual{};
      glReadPixels(0, 0, 16, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
      for (int x = 0; x < 16; ++x)
      {
        glm::ivec3 cell(center);
        cell[u] = origin[u] + x;
        const int distance = std::abs(cell.x - emitter.x) +
                             std::abs(cell.y - emitter.y) +
                             std::abs(cell.z - emitter.z);
        const int sky = phase == 2 ? 0 : 15;
        // The test catalog's fire emits14; AIR attenuates by1 per axial step.
        const int block = phase == 1 ? std::max(0, 14 - distance) : 0;
        const std::array<uint8_t, 4> expected =
            physical && invalid ? std::array<uint8_t, 4>{0, 0, 0, 0}
            : physical ? std::array<uint8_t, 4>{uint8_t(sky * 17),
                                                uint8_t(block * 17), 0, 255}
                       : std::array<uint8_t, 4>{255, 255, 0, 255};
        const std::array<uint8_t, 4> value{actual[x * 4], actual[x * 4 + 1],
                                           actual[x * 4 + 2],
                                           actual[x * 4 + 3]};
        if (value != expected)
          std::cerr << "surface packed=" << packed << " phase=" << phase
                    << " face=" << face << " cell=" << cell.x << ',' << cell.y
                    << ',' << cell.z << " actual=" << int(value[0]) << ','
                    << int(value[1]) << ',' << int(value[3])
                    << " expected=" << int(expected[0]) << ','
                    << int(expected[1]) << ',' << int(expected[3]) << '\n';
        Expect(
            value == expected,
            "production surface fragment matches independent per-cell light");
      }
      Expect(glGetError() == GL_NO_ERROR,
             "surface physical draw has no GL errors");
    }
    if (mdi)
      draw_table.SignalDrawComplete();
  };
  for (int phase = 0; phase < 3; ++phase)
  {
    if (phase == 1)
    {
      const auto previous_words = binding->ShaderWords();
      world.SetBlock(emitter,
                     kFire); // diagonal source, outside seven geometry inputs
      field = calculate();
      Expect(field.MatchesGeometry(binding->GeometryInputs()) &&
                 owner.Refresh(binding, 19, field) &&
                 binding->ShaderWords() == previous_words,
             "surface field changes without geometry or handle replacement");
    }
    else if (phase == 2)
    {
      world.SetBlock(emitter, BLOCK_AIR);
      for (int x = origin.x - 16; x < origin.x + 32; ++x)
        for (int z = origin.z - 16; z < origin.z + 32; ++z)
          world.SetBlock({x, 15, z}, kStone);
      field = calculate();
      binding = owner.Prepare(19, field.GeometryInputs(), field);
      Expect(binding != nullptr,
             "canonical dark surface owns actual zero field");
    }
    for (int face = 0; face < 6; ++face)
      draw(face, phase, true, false);
    if (!packed)
      draw(127, phase, true, false);
  }
  draw(4, 2, true, true);
  draw(4, 2, false,
       true); // Flat/baked mode never reads invalid physical handle.
  SectionRenderLightBindings fallback;
  Expect(fallback.BeginWorld(19, field.GeometryInputs().world_geometry_identity,
                             1, SectionLightVolume::CellCount, 1, true),
         "surface minimal-capacity owner initializes");
  binding = fallback.Prepare(19, field.GeometryInputs(), field);
  Expect(binding && !fallback.IsResident(binding),
         "surface minimal-capacity field uses ordered fallback");
  draw_owner = &fallback;
  for (int face = 0; face < 6; ++face)
    draw(face, 2, true, false);
  program.Unuse();
  if (mdi)
  {
    draw_table.Reset();
    UGreedyGpuBackend backend;
    backend.DestroyPass(mdi_cache);
    glDeleteBuffers(1, &ebo);
  }
  glBindVertexArray(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (quads)
    glDeleteBuffers(1, &quads);
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &color);
  glDeleteTextures(1, &texture);
}

static void
TestCrossCanonicalDark(cutum::UBlockRegistry &registry,
                       const cutum::UBlockDefinitionStorage *definitions,
                       bool gles)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  world.SetBlock({4, 4, 4}, kTallGrass);
  // Every path around this roof to the sample exceeds the maximum indirect
  // light reach of15cells; no emitter is present. Expected sky/block is0/0.
  for (int x = -16; x < 32; ++x)
    for (int z = -16; z < 32; ++z)
      world.SetBlock({x, 15, z}, kStone);
  RelightJobSpec spec;
  spec.min_world_y = 0;
  spec.max_world_y = 31;
  const auto field =
      UChunkRelightSnapshot::CaptureSections(world, spec, {{0, 0, 0}})
          .Compute(registry);
  Expect(field.inputs_complete && field.chunks.size() == 1 &&
             field.chunks[0]
                     .padded_light->cells[SectionLightVolume::Index(4, 4, 4)] ==
                 0,
         "roofed Cross fixture independently expects canonical zero light");
  world.GetChunkManager().ReplaceComputedSectionLightVolume(
      {0, 0, 0}, *field.chunks[0].padded_light, true, true);
  Expect(world.RecordLightCalculation(
             field, registry.GetDefinitionsCatalogSnapshot()) == 1,
         "roofed Cross fixture records completed physical proof");
  const auto light = world.CaptureRenderLightSnapshot({0, 0, 0});
  SectionRenderLightBindings owner;
  auto binding = owner.PrepareForWorld(37, light->GeometryInputs(), *light);
  Expect(bool(binding), "canonical dark Cross source obtains a valid binding");
  CrossInstanceBatch batch;
  batch.blockId = kTallGrass;
  CrossInstanceGpu instance;
  instance.center = {4, 4, 4};
  instance.skyLight = instance.blockLight =
      1; // Deliberately wrong baked values.
  instance.renderLightHandle = binding->ShaderWords();
  AppendCrossInstance(batch, instance, binding);
  UCrossGpuBackend backend;
  CrossGpuPassCache pass;
  Expect(backend.RefreshPass(pass, {batch}, 1, 1, 1, 1),
         "canonical dark Cross geometry publishes");
  ProbeCrossPhysicalShader(backend, pass.batches[0], owner, binding, 0,
                           {4, 4, 4}, {0, 0, 1, 1}, gles);
  ProbeCrossPhysicalShader(backend, pass.batches[0], owner, binding, 0,
                           {4, 4, 4}, {15, 15, 1, 1}, gles, true, true, false);
  backend.DestroyAll(pass);
}
#endif

#if defined(SECTION_LIGHT_BINDING_DRIVER)
static void
TestOrderedFieldBindings(cutum::UBlockRegistry &registry,
                         const cutum::UBlockDefinitionStorage *definitions)
{
  using namespace cutum;
  UBlockWorld world;
  world.SetFluidDefinitions(definitions);
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  world.SetBlock({4, 4, 4}, kStone);
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  std::vector<glm::ivec3> targets;
  for (int x = 0; x < 7; ++x)
    targets.push_back({x, 0, 0});
  RelightJobSpec spec;
  spec.min_world_y = 0;
  spec.max_world_y = 15;
  spec.include_known_air_outputs = true;
  const auto initial =
      UChunkRelightSnapshot::CaptureSections(world, spec, targets)
          .Compute(registry);
  Expect(initial.inputs_complete && initial.chunks.size() == targets.size(),
         "ordered resource fixture computes all requested physical fields");
  for (const auto &output : initial.chunks)
  {
    Expect(bool(output.padded_light), "computed field retains its padded volume");
    world.GetChunkManager().ReplaceComputedSectionLightVolume(
        output.coord, *output.padded_light, true, true);
  }
  Expect(world.RecordLightCalculation(initial, catalog) == targets.size(),
         "ordered resource fixture has seven actual certified physical fields");
  const auto root = world.CaptureRenderLightSnapshot({0, 0, 0});
  Expect(bool(root), "root physical field is available");
  constexpr uint64_t epoch = 17;
  SectionRenderLightBindings owner;
  Expect(!owner.BeginWorld(0, root->GeometryInputs().world_geometry_identity),
         "zero source epoch cannot initialize physical resource authority");
  Expect(owner.BeginWorld(epoch, root->GeometryInputs().world_geometry_identity,
                          8, 8u * SectionLightVolume::CellCount, 8, true),
         "bounded physical owner reserves ordered fallback inside eight pages");
  std::vector<std::shared_ptr<SectionRenderLightBinding>> bindings;
  for (auto coord : targets)
  {
    const auto field = world.CaptureRenderLightSnapshot(coord);
    Expect(bool(field), "each requested physical field is computed");
    auto binding = owner.Prepare(epoch, field->GeometryInputs(), *field);
    Expect(binding && binding->IsLive() && owner.IsResident(binding),
           "all seven permanent page/table slots are genuinely occupied");
    bindings.push_back(std::move(binding));
  }
  const auto original_words = bindings[0]->ShaderWords();
  world.SetBlock({16, 8, 16}, kFire);
  const auto changed =
      UChunkRelightSnapshot::CaptureSections(world, spec, {{0, 0, 0}})
          .Compute(registry);
  Expect(changed.inputs_complete && changed.chunks.size() == 1 &&
             bool(changed.chunks[0].padded_light),
         "diagonal emitter computes a complete successor field");
  world.GetChunkManager().ReplaceComputedSectionLightVolume(
      changed.chunks[0].coord, *changed.chunks[0].padded_light, true, true);
  Expect(world.RecordLightCalculation(changed, catalog) == 1,
         "diagonal emitter installs new physical light without root geometry "
         "edit");
  const auto updated = world.CaptureRenderLightSnapshot({0, 0, 0});
  Expect(
      updated && updated->MatchesGeometry(bindings[0]->GeometryInputs()) &&
          updated->FieldRevision() != root->FieldRevision(),
      "field-only successor is compatible and has changed physical revision");
  Expect(owner.Refresh(bindings[0], epoch, *updated) && bindings[0]->IsLive() &&
             !owner.IsResident(bindings[0]) &&
             bindings[0]->ShaderWords() == original_words,
         "full atlas field refresh defers sampling without changing geometry "
         "handles");
  ProbeCertifiedBinding(owner, bindings[0]);
  for (size_t n = 1; n < bindings.size(); ++n)
    ProbeCertifiedBinding(owner, bindings[n]);
  owner.CollectRetired();
  Expect(
      owner.PrepareDraw(bindings[0]).has_value() &&
          owner.IsResident(bindings[0]) &&
          bindings[0]->ShaderWords() == original_words,
      "retired page recovery promotes field without geometry handle rewrite");
  ProbeCertifiedBinding(owner, bindings[0]);
  SectionRenderLightBindings minimum;
  Expect(minimum.BeginWorld(epoch,
                            root->GeometryInputs().world_geometry_identity, 1,
                            SectionLightVolume::CellCount, 1, true),
         "single-page physical owner initializes ordered-only budget");
  auto deferred = minimum.Prepare(epoch, updated->GeometryInputs(), *updated);
  Expect(deferred && deferred->IsLive() && !minimum.IsResident(deferred) &&
             deferred->ShaderWords() == std::array<uint32_t, 4>{},
         "metadata/page exhaustion retains certified source for explicit "
         "ordered draw");
  ProbeCertifiedBinding(minimum, deferred);
  auto other = minimum.Prepare(epoch, bindings[1]->GeometryInputs(),
                               bindings[1]->LightSnapshot());
  Expect(other && other->IsLive(),
         "second geometry artifact needs no additional GPU slot");
  ProbeCertifiedBinding(minimum, other);
  ProbeCertifiedBinding(minimum, deferred);
  Expect(!minimum.Prepare(epoch + 1, updated->GeometryInputs(), *updated) &&
             !minimum.Refresh(deferred, epoch + 1, *updated),
         "capacity fallback cannot bypass source epoch proof");
  minimum.Reset();
  Expect(!deferred->IsLive() && !other->IsLive() &&
             !minimum.PrepareDraw(deferred),
         "world reset invalidates CPU-backed fallback leases too");
}
#endif

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

static void TestCatalogPublicationRoots(const char *report_path,bool gpu)
{
  using namespace cutum;using Json=nlohmann::json;
  struct Epoch {
    UBlockWorld world;UChunkMeshCache cache;SectionRenderLightBindings resources;
    std::unique_ptr<MeshBuildResult> returned;
    std::shared_ptr<const SectionNoSurfaceProof> no_surface;
    std::weak_ptr<const BlockDefinitionCatalog> old;
  } first,second;
  auto definitions=MakeDefinitions();UBlockRegistry registry(nullptr,definitions);
  const auto publish=[&](char tag){
    auto catalog=definitions->GetCatalogSnapshot();auto by_id=catalog->ById;auto names=catalog->NameToId;
    by_id.at(kStone).DisplayName=std::string(32768,tag);
    definitions->ReplaceAll(std::move(by_id),std::move(names));
  };
  const auto build=[&](Epoch &epoch,char tag){
    publish(tag);epoch.old=definitions->GetCatalogSnapshot();
    auto &world=epoch.world;auto &cache=epoch.cache;
    world.SetFluidDefinitions(definitions.get());world.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
    const glm::ivec3 root(0);world.SetBlock({4,4,4},kStone);
    RelightJobSpec spec;spec.min_world_y=world.GetVerticalProfile().MinY;spec.max_world_y=world.GetVerticalProfile().MaxY;
    auto result=UChunkRelightSnapshot::CaptureSections(world,spec,{root}).Compute(registry);
    Expect(result.inputs_complete && result.chunks.size()==1 && result.content_certificate,"catalog roots use real solver-created certificate");
    world.GetChunkManager().GetChunk(root)->ReplaceComputedLightVolume(*result.chunks[0].padded_light,true,true);
    Expect(world.RecordLightCalculation(result,definitions->GetCatalogSnapshot())==1,"catalog roots install real two-domain field proof");
    auto source=world.CaptureRenderLightSnapshot(root);Expect(bool(source),"catalog roots capture real certified field");
    if(gpu)Expect(epoch.resources.BeginWorld(cache.GetCaptureStore().WorldEpoch(),source->GeometryInputs().world_geometry_identity,16,8u*SectionLightVolume::CellCount,8),"catalog roots initialize real GL binding store");
    cache.SetPrepareRenderLightFn([&epoch,gpu](uint64_t version,const SectionGeometryRenderInputs &geometry,const SectionRenderLightSnapshot &light)->std::shared_ptr<SectionRenderLightBinding>{
      if(gpu)return epoch.resources.Prepare(version,geometry,light);
      return std::make_shared<TestRenderLightBinding>(version,geometry,light);
    });
    auto settings=MakeFrustumMeshSettings();settings.AsyncMeshing=false;settings.FrustumCulling=false;cache.SetRenderSettings(settings);
    cache.RebuildChunkImmediate(world,registry,root);
    auto geometry=cache.GetPublishedGeometryRenderInputs(root);auto binding=cache.GetPublishedRenderLightBinding(root);
    Expect(geometry && geometry->catalog==definitions->GetCatalogSnapshot() && binding && binding->IsLive(),"real CPU mesh publication retains catalog in geometry and field binding");
#if defined(SECTION_LIGHT_BINDING_DRIVER)
    if(gpu)ProbeCertifiedBinding(epoch.resources,binding);
#endif
    auto snapshot=ChunkMeshCaptureAdmissionTestAccess::CaptureOwned(cache,world,root);
    UAsyncMeshBuilder builder(1);Expect(builder.Enqueue(snapshot,registry) && builder.WaitIdleFor(std::chrono::seconds(10)),"actual worker returns catalog-bearing mesh result");
    auto completed=builder.DrainCompleted(1);Expect(completed.size()==1 && completed[0].InputCatalog,"returned mesh result owns immutable input catalog");
    epoch.returned=std::make_unique<MeshBuildResult>(std::move(completed[0]));
    auto stored=cache.GetCaptureStore().CaptureAndStore(world,root,cache.GetChunkMeshRevision(root));
    Expect(bool(stored),"actual capture cache owns its snapshot");
    world.GetChunkManager().EnsureChunk({2,0,0});
    auto proof=UGreedyMesher::CaptureNoSurface(world,{2,0,0},registry,definitions->GetCatalogSnapshot());
    Expect(bool(proof),"actual mesher creates no-surface proof with owned geometry catalog");
    epoch.no_surface=std::make_shared<SectionNoSurfaceProof>(std::move(*proof));
  };
  build(first,'a');build(second,'b');publish('c');UBlockDefinitionStorage::ReleaseThreadReads();
  auto current=definitions->GetCatalogSnapshot();
  Expect(!first.old.expired() && !second.old.expired(),"distinct old payloads remain after actual current publication replacement");
  const auto visit=[&](CatalogPublicationRootInspector &out,Epoch &epoch){epoch.world.InspectCatalogRoots(out);epoch.cache.InspectCatalogRoots(out);out.MeshResult(*epoch.returned);out.NoSurface(epoch.no_surface);};
  CatalogPublicationRootInspector observer;observer.BlockCatalog(current);visit(observer,first);visit(observer,second);
  const auto union_census=observer.Catalogs();
  Expect(union_census.Objects(CatalogObjectKind::Blocks)==3 && union_census.text_bytes>=3*32769 && !union_census.overflow,"publication graph finds current plus two independent old catalog payloads and authored name capacities");
  Expect(observer.Counts.Nodes(CatalogPublicationRootInspector::Node::Certificate)==2 && observer.Counts.Nodes(CatalogPublicationRootInspector::Node::Binding)==2 && observer.Counts.Nodes(CatalogPublicationRootInspector::Node::Mesh)==2,"real result/certificate/binding nodes counted once");
  Expect(observer.Counts.Nodes(CatalogPublicationRootInspector::Node::NoSurface)==2,"both returned mesher-created proof catalog owners included");
  CatalogPublicationRootInspector fields,caches,omitted,aliases;
  fields.BlockCatalog(current);first.world.InspectCatalogRoots(fields);second.world.InspectCatalogRoots(fields);
  caches.BlockCatalog(current);first.cache.InspectCatalogRoots(caches);second.cache.InspectCatalogRoots(caches);
  omitted.BlockCatalog(current);
  Expect(fields.Catalogs().Objects(CatalogObjectKind::Blocks)==3 && caches.Catalogs().Objects(CatalogObjectKind::Blocks)==3 && omitted.Catalogs().Objects(CatalogObjectKind::Blocks)==1,"field and published mesh owners independently retain both old versions;omitted-root control misses them");
  for(int i=0;i<64;++i){visit(aliases,second);aliases.BlockCatalog(current);visit(aliases,first);}
  Expect(aliases.Catalogs().known_storage_bytes==union_census.known_storage_bytes && aliases.Catalogs().shared_owners==union_census.shared_owners && aliases.Catalogs().objects==union_census.objects,"64 alias/permuted root traversals deduplicate all catalog payloads and controls");
  auto escaped_field=first.world.GetChunkManager().GetSectionLightField({0,0,0});
  auto escaped_context=first.world.GetRecordedLightInputContext({0,0,0},LightDomain::Sky);
  auto escaped_binding=first.cache.GetPublishedRenderLightBinding({0,0,0});
  SectionGeometryRenderInputs invalid;invalid.catalog=escaped_context->input_catalog;
  CatalogPublicationRootInspector invalid_observer;invalid_observer.Geometry(invalid);
  Expect(!invalid.valid && invalid_observer.Catalogs().Objects(CatalogObjectKind::Blocks)==1,"invalid geometry still has an owning catalog slot");invalid={};
  for(auto *epoch:{&first,&second}){epoch->cache.RemoveChunk({0,0,0});epoch->cache.GetCaptureStore().InvalidateAll();epoch->returned.reset();epoch->no_surface.reset();epoch->world.Clear();epoch->resources.Reset();}
  UBlockDefinitionStorage::ReleaseThreadReads();
  Expect(second.old.expired() && !first.old.expired(),"collectors retain no strong payload after real owner removal;escaped first roots remain");
  CatalogPublicationRootInspector escaped;escaped.LightField(escaped_field);escaped.RelightResult(escaped_context);escaped.Binding(escaped_binding);
  Expect(escaped.Catalogs().Objects(CatalogObjectKind::Blocks)==1,"escaped field/context/binding join one old catalog");
  if(gpu)Expect(!escaped_binding->IsLive(),"reset real GL binding remains an owning CPU object");
  escaped_binding.reset();escaped_field.reset();
  std::thread final_owner([context=std::move(escaped_context)]()mutable{context.reset();});final_owner.join();
  Expect(first.old.expired(),"last cross-thread explicit input context retires old payload despite live weak-only inspectors");
  CatalogPublicationRootInspector final_graph;final_graph.BlockCatalog(current);first.world.InspectCatalogRoots(final_graph);first.cache.InspectCatalogRoots(final_graph);second.world.InspectCatalogRoots(final_graph);second.cache.InspectCatalogRoots(final_graph);
  Expect(final_graph.Catalogs().Objects(CatalogObjectKind::Blocks)==1,"cleared production graph has only current catalog");
  Json report={{"schema","catalog-publication-root-graph.v2"},{"status","PASS"},{"real_GL",gpu},{"old_distinct_versions",2},{"union_catalogs",3},{"union_known_catalog_bytes",union_census.known_storage_bytes},{"omitted_root_catalogs",1},{"certificate_nodes",observer.Counts.Nodes(CatalogPublicationRootInspector::Node::Certificate)},{"binding_nodes",observer.Counts.Nodes(CatalogPublicationRootInspector::Node::Binding)},{"no_surface_proof_nodes",observer.Counts.Nodes(CatalogPublicationRootInspector::Node::NoSurface)},{"result_nodes",observer.Counts.Nodes(CatalogPublicationRootInspector::Node::Relight)},{"field_nodes",observer.Counts.Nodes(CatalogPublicationRootInspector::Node::Field)},{"catalog_reference_visits",observer.Counts.catalog_handles},{"alias64_payload_deduplicated",true},{"invalid_owning_geometry_counted",true},{"observer_does_not_retain_payload",true},{"last_cross_thread_payloads",0},{"final_catalogs",1},{"real_GL_readbacks",gpu?2:0}};
  std::ofstream output(report_path);output<<report.dump(2)<<'\n';Expect(bool(output),"catalog root report writes");std::cout<<report.dump()<<'\n';
}

int main(int argc, char **argv)
{
  const std::shared_ptr<cutum::UBlockDefinitionStorage> definitions =
      MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);

  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());

  if(argc==3 && std::string(argv[1])=="--catalog-publication-roots"){
    TestCatalogPublicationRoots(argv[2],false);return 0;
  }

#if defined(SECTION_LIGHT_BINDING_DRIVER)
  const bool gles_visibility_driver =
      argc == 2 && std::string(argv[1]) == "--face-visibility-gles-driver";
  const bool gles_cross_driver =
      argc == 2 && std::string(argv[1]) == "--cross-light-gles-driver";
  const bool gles_context = gles_visibility_driver || gles_cross_driver;
  const bool visibility_driver =
      gles_visibility_driver ||
      (argc == 2 && std::string(argv[1]) == "--face-visibility-driver");
  const bool packed_lifecycle_driver =
      argc == 2 &&
      std::string(argv[1]) == "--gpu-geometry-light-lifecycle-driver";
  const bool catalog_roots_driver=argc==3 && std::string(argv[1])=="--catalog-publication-roots-driver";
  if (catalog_roots_driver || visibility_driver || gles_cross_driver || packed_lifecycle_driver ||
      (argc == 2 && std::string(argv[1]) == "--light-binding-driver"))
  {
    if (!glfwInit())
      return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API,
                   gles_context ? GLFW_OPENGL_ES_API : GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,
                   (visibility_driver && !gles_visibility_driver) ||
                           packed_lifecycle_driver
                       ? 4
                       : 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, gles_context ? 1 : 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, gles_context
                                            ? GLFW_OPENGL_ANY_PROFILE
                                            : GLFW_OPENGL_CORE_PROFILE);
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
    if ((visibility_driver || packed_lifecycle_driver) &&
        (!glDispatchCompute || !glMemoryBarrier || !glBindBufferBase))
    {
      glfwDestroyWindow(window);
      glfwTerminate();
      return 77;
    }
    std::cout << "GPU: " << glGetString(GL_RENDERER)
              << " GL: " << glGetString(GL_VERSION) << '\n';
    if(catalog_roots_driver){
      TestCatalogPublicationRoots(argv[2],true);
      Expect(glGetError()==GL_NO_ERROR,"real catalog root GL lifecycle leaves no error");
      glfwDestroyWindow(window);glfwTerminate();return 0;
    }
    if (packed_lifecycle_driver)
    {
      TestPhysicalGeometryCapture(registry, definitions.get(), true);
      TestGpuStagingSlotOwnership();
      TestGpuReadbackOwnership();
      TestGpuTicketStorageReplacement(registry, definitions.get());
      TestGpuCapturePolicyTransitions(registry, definitions.get());
      TestPackedGeometryWaitsForPhysicalLight(registry, definitions.get());
      Expect(glGetError() == GL_NO_ERROR, "packed lifecycle has no GL errors");
      glfwDestroyWindow(window);
      glfwTerminate();
      std::cout << "gpu_geometry_light_lifecycle_driver_test: PASS\n";
      return 0;
    }
    TestShaderProgramFailureAtomicity(gles_context);
    TestProductionGreedyPrograms(gles_context,
                                 visibility_driver && !gles_visibility_driver);
    TestSurfacePhysicalField(registry, definitions.get(), gles_context, false);
    TestSurfacePhysicalField(registry, definitions.get(), gles_context, false,
                             true);
    if (visibility_driver && !gles_visibility_driver)
    {
      TestMdiLightDrawTable();
      TestSurfacePhysicalField(registry, definitions.get(), false, false, false,
                               true);
      TestSurfacePhysicalField(registry, definitions.get(), false, true);
    }
    if (visibility_driver)
      TestMaterialVisibilityParity(registry, definitions.get(), true,
                                   gles_visibility_driver);
    else if (gles_cross_driver)
    {
      TestCrossLightArtifactTransport(registry, definitions.get(), true, true);
      TestCrossPassFailureAtomicity(registry, definitions.get(), true);
      TestCrossCanonicalDark(registry, definitions.get(), true);
    }
    else
    {
      TestNoDrawPhysicalResourceAdmission(registry, definitions.get());
      TestCachedPreviewAfterProofOnlyCompletion(registry, definitions.get());
      TestCertifiedGeometrySurvivesInflightLight(registry, definitions.get(),
                                                 true);
      TestPublishedLightRefreshLifecycle(registry, definitions.get());
      TestPublishedLightRefreshFairness(registry, definitions.get());
      TestGeometryWaitsForPhysicalLight(registry, definitions.get(), true);
      TestGeometryWaitsForPhysicalLight(registry, definitions.get(), true,
                                        true);
      TestFirstComputedLightPublication(registry, definitions.get(), true);
      TestOrderedFieldBindings(registry, definitions.get());
      TestCrossLightArtifactTransport(registry, definitions.get(), true);
      TestCrossPassFailureAtomicity(registry, definitions.get(), false);
      TestCrossCanonicalDark(registry, definitions.get(), false);
    }
    Expect(glGetError() == GL_NO_ERROR,
           "certified binding integration has no GL errors");
    glfwDestroyWindow(window);
    glfwTerminate();
    if (visibility_driver)
    {
      std::cout << "gpu_material_visibility_driver_test: PASS (actual "
                   "production compute mask)\n";
      return 0;
    }
    std::cout << "section_render_light_bindings_driver_test: PASS "
                 "(physical proof/publication/resource lifetime; no world "
                 "shader acceptance)\n";
    return 0;
  }
#endif
  TestFluidMapMutationDelivery(registry, definitions.get());
  TestCrossGrassVisibleWithFrustum(world, registry);
  TestCampfireStoneMesh(world, registry);
  TestGreedyAlphaCutoutIsMaterialScoped(world, registry);
  TestOverflowRetryWithNoDirtyAdmission(registry);
  TestNoSurfaceGeometryProof(registry, definitions.get());
  TestSecondPlaneGeometryParity(registry, definitions.get());
  TestDenseAirMeshCapture(registry);
  TestPhysicalCaptureOwnership(registry, definitions.get());
  TestMeshResultCaptureOwnership(registry, definitions.get());
  TestUnknownGeometryDefersPhysicalBuild(registry, definitions.get());
  TestNoSurfaceRepairStability(registry, definitions.get());
  TestEmptyPhysicalFluidPublication(registry, definitions.get());
  TestPhysicalGeometryCapture(registry, definitions.get());
  TestMaterialVisibilityParity(registry, definitions.get(), false);
  TestOccludedMeshDependencyRepair(registry);
  TestPublicationRefreshesDrawRefs(registry);
  TestNoDrawPhysicalResourceAdmission(registry, definitions.get());
  TestCachedPreviewAfterProofOnlyCompletion(registry, definitions.get());
  TestSelectiveMeshLeaseWorkerLifecycle(registry, definitions.get());
  TestMeshSameKeySupersession(registry, definitions.get());
  TestCancelledMeshRetryReceipts(registry, definitions.get());
  const bool same_frame_ok = TestSameFrameDirtyCoalescing(registry, definitions.get());
  const bool deferred_ok = TestDeferredRemeshRetirement(registry, definitions.get());
  const bool deferred_capacity_ok = TestDeferredRemeshCapacity(registry, definitions.get());
  const bool retry_raa_ok = TestRetryReceiptAndRealRaa(registry, definitions.get());
  TestOutsideRadiusCpuMeshLifetime(registry, definitions.get());
  Expect(same_frame_ok && deferred_ok && deferred_capacity_ok && retry_raa_ok, "dirty and deferred owners survive zero quota and retire on explicit removal/reset");
  const bool admission_owner_ok = TestMeshAdmissionOwnerLifetime(registry, definitions.get());
  const bool admission_debt_ok = TestCacheMeshAdmissionDebt(registry, definitions.get());
  Expect(TestCaptureStoreDemandLifetime(registry, definitions.get()), "capture store outcomes retain demand and publish current successor");
  Expect(admission_owner_ok && admission_debt_ok, "mesh admission rejects preserve credits owner and executable current debt");
  TestForgottenMeshJobLifetime(registry, definitions.get());
  TestCacheMeshRemovalLifetime(registry, definitions.get());
  TestCacheMeshCancellationLifetime(registry, definitions.get());
  TestCertifiedGeometrySurvivesInflightLight(registry, definitions.get());
  TestScreenRayFirstPublication(registry, definitions.get());
  TestScreenRayFirstPublication(registry, definitions.get(), 1);
  TestScreenRayFirstPublication(registry, definitions.get(), 2);
  TestPublishedLightRefreshLifecycle(registry, definitions.get());
  TestPublishedLightRefreshFairness(registry, definitions.get());
  TestGeometryWaitsForPhysicalLight(registry, definitions.get());
  TestGeometryWaitsForPhysicalLight(registry, definitions.get(), false, false,
                                    true);
  TestGeometryWaitsForPhysicalLight(registry, definitions.get(), false, false,
                                    true, true);
  TestGeometryWaitsForPhysicalLight(registry, definitions.get(), false, true);
  TestGeometryLightQueueCapacity(registry, definitions.get());
  TestFirstComputedLightPublication(registry, definitions.get());
  TestCrossLightArtifactTransport(registry, definitions.get());
  TestWorldDemandIsolation();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
