#pragma once

#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Engine/TerrainGpuBufferReport.h"
#include "Render/Gl/ComputeBufferBindings.h"
#include "Render/Gl/ComputeProgram.h"
#include "Render/Gl/PersistentComputeStorage.h"
#include "Render/Mesh/GpuFrustumCull.h"
#include "Test/CreatureItemIconStorageFixture.h"
#include "World/Core/World.h"
#include "World/Lighting/GpuLightTransport.h"
#include "World/Mesh/WorldMeshService.h"
#include <cstdlib>
#include <optional>

namespace cutum::compute_gpu_storage_fixture
{
using Role = TerrainGpuBufferRole;
using gui_context_fixture::Require;
using shared_content_preview_fixture::Binding;

class Calls
{
public:
  explicit Calls(Role role, size_t refuse = 0) : RoleForBuffers(role), Refuse(refuse)
  {
    Active = this;
    __glewBufferData = Upload;
    __glewDispatchCompute = Dispatch;
    __glewGenBuffers = Generate;
  }
  ~Calls()
  {
    __glewBufferData = OriginalUpload;
    __glewDispatchCompute = OriginalDispatch;
    __glewGenBuffers = OriginalGenerate;
    Active = nullptr;
  }
  size_t uploads{}, dispatches{}, generated{};
  TerrainGpuBufferCensus buffers;
  TerrainGpuBufferSnapshot atFirstDispatch;

private:
  Role RoleForBuffers;
  size_t Refuse;
  PFNGLBUFFERDATAPROC OriginalUpload = __glewBufferData;
  PFNGLDISPATCHCOMPUTEPROC OriginalDispatch = __glewDispatchCompute;
  PFNGLGENBUFFERSPROC OriginalGenerate = __glewGenBuffers;
  static inline Calls *Active{};

  static void GLAPIENTRY Upload(GLenum target, GLsizeiptr bytes, const void *data, GLenum usage)
  {
    auto &probe = *Active;
    if (target == GL_SHADER_STORAGE_BUFFER)
    {
      probe.buffers.Add(probe.RoleForBuffers, static_cast<GLuint>(Binding(GL_SHADER_STORAGE_BUFFER_BINDING)));
      if (++probe.uploads == probe.Refuse)
        bytes = -1;
    }
    probe.OriginalUpload(target, bytes, data, usage);
  }

  static void GLAPIENTRY Dispatch(GLuint x, GLuint y, GLuint z)
  {
    auto &probe = *Active;
    if (probe.dispatches++ == 0)
      probe.atFirstDispatch = probe.buffers.Read();
    probe.OriginalDispatch(x, y, z);
  }

  static void GLAPIENTRY Generate(GLsizei count, GLuint *names)
  {
    Active->generated += count;
    Active->OriginalGenerate(count, names);
  }
};

struct FloodEnvironment
{
  static constexpr const char *Key = "CUBATARIUM_GPU_BLOCKLIGHT_COMPUTE";
  std::optional<std::string> Previous;
  FloodEnvironment()
  {
    if (const char *value = std::getenv(Key))
      Previous = value;
    Set("1");
  }
  ~FloodEnvironment() { Set(Previous ? Previous->c_str() : nullptr); }
  static void Set(const char *value)
  {
#if defined(_WIN32)
    _putenv_s(Key, value ? value : "");
#else
    if (value)
      setenv(Key, value, 1);
    else
      unsetenv(Key);
#endif
  }
};

inline TerrainGpuBufferCensus Collect()
{
  TerrainGpuBufferCensus out;
  InspectPersistentComputeGpuBuffers(out);
  return out;
}

inline nlohmann::json Run()
{
  creature_item_icon_fixture::PlatformFixture platform;
  auto world = std::make_unique<UWorld>(nullptr, nullptr);
  UBlockRegistry registry(nullptr, std::make_shared<UBlockDefinitionStorage>());
  auto &cache = world->GetMeshService().GetCache();
  Require(!Collect().Read().references, "compute stores begin empty in the current context");
  struct Cleanup { ~Cleanup() { ReleasePersistentComputeGpuStorage(); } } cleanup;
  GLint originalBuffer = Binding(GL_SHADER_STORAGE_BUFFER_BINDING);
  GLint originalProgram = Binding(GL_CURRENT_PROGRAM);
  const ComputeBufferBindings<4> restore;
  GLint alignment{};
  glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &alignment);
  Require(alignment > 0, "query the driver's SSBO range alignment");
  std::array<GLuint, 4> sentinels{};
  glGenBuffers(static_cast<GLsizei>(sentinels.size()), sentinels.data());
  for (GLuint i = 0; i < sentinels.size(); ++i)
  {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sentinels[i]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, alignment * 2, nullptr, GL_STATIC_DRAW);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, i, sentinels[i], alignment, alignment);
  }
  UComputeProgram sentinelProgram;
  Require(sentinelProgram.CompileSource("#version 430 core\nlayout(local_size_x=1) in;\nvoid main() {}"), "build caller program");
  glUseProgram(sentinelProgram.Program());
  const auto preserved = [&] {
    if (Binding(GL_CURRENT_PROGRAM) != GLint(sentinelProgram.Program()) ||
        Binding(GL_SHADER_STORAGE_BUFFER_BINDING) != GLint(sentinels.back()))
      return false;
    for (GLuint i = 0; i < sentinels.size(); ++i)
    {
      GLint buffer{};
      GLint64 start{}, size{};
      glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &buffer);
      glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, i, &start);
      glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, i, &size);
      if (GLuint(buffer) != sentinels[i] || start != alignment || size != alignment)
        return false;
    }
    return true;
  };
  const auto extent = [](const TerrainGpuBufferSnapshot &s, Role role) { return s.roles[size_t(role)].bytes; };

  {
    Calls probe(Role::FluidColumnScan);
    UGpuFrustumCull cull;
    cull.RebuildVisible(cache, nullptr, nullptr, 0.f);
    Require(!probe.generated && !probe.dispatches && preserved(), "CPU visibility rebuild admits no unused GPU cull storage");
  }

  constexpr int height = 7;
  constexpr size_t columns = CHUNK_SIZE * CHUNK_SIZE;
  std::vector<uint8_t> flags(columns * height);
  flags[2 + columns] = flags[2 + columns * 6] = flags[5] = 1;
  std::vector<int16_t> expected(columns, -1);
  expected[2] = 6;
  expected[5] = 0;
  std::vector<int16_t> tops;
  Require(TryGpuScanFluidColumns(flags.data(), height, tops) && tops == expected && !Collect().Read().references,
          "desktop frame path preserves the CPU scan and acquires no SSBOs");
  tops = {17};
  {
    Calls refused(Role::FluidColumnScan, 2);
    Require(!TryGpuDispatchFluidColumns(flags.data(), height, tops) && tops == std::vector<int16_t>{17} &&
                refused.uploads == 2 && !refused.dispatches && refused.buffers.Read().namesWithoutObject == 2 && preserved(),
            "refuse one fluid allocation before dispatch and retire both candidates");
  }
  {
    Calls retry(Role::FluidColumnScan);
    Require(TryGpuDispatchFluidColumns(flags.data(), height, tops) && tops == expected && retry.dispatches == 1 && preserved(),
            "fluid compute retry returns the independent column tops");
  }

  std::vector<GreedyBatchRef> refs(4);
  for (int i = 0; i < 4; ++i)
    refs[i].chunkCoord.x = i;
  {
    Calls refused(Role::TransparentSort, 1);
    Require(!TryGpuSortTransparentGreedyBatches(refs, cache, glm::vec3(0.f), registry) &&
                !refused.dispatches && refused.buffers.Read().namesWithoutObject == 1 && preserved(),
            "refused transparent key store retires before sorting");
    for (int i = 0; i < 4; ++i)
      Require(refs[i].chunkCoord.x == i, "failed sort preserves its refs");
  }
  {
    Calls retry(Role::TransparentSort);
    Require(TryGpuSortTransparentGreedyBatches(refs, cache, glm::vec3(0.f), registry) &&
                retry.dispatches == 3 && preserved(), "transparent key compute retries with four test refs");
    std::array<bool, 4> seen{};
    for (const auto &ref : refs)
    {
      const int index = ref.chunkCoord.x;
      Require(index >= 0 && index < 4 && !seen[index], "GPU sort returns a permutation of the refs");
      seen[index] = true;
    }
  }

  world->GetBlockWorld().GetChunkManager().EnsureChunk({0, 0, 0});
  {
    FloodEnvironment environment;
    Calls flood(Role::BlocklightFlood);
    Require(TryGpuPropagateBlocklight(world->GetBlockWorld(), registry, {0, 0, 0}) &&
                flood.dispatches == 15 && preserved(), "opt-in flood stages the four real buffers and restores caller state");
  }

  LightTransportInput input;
  input.width = 4;
  input.height = 3;
  input.depth = 2;
  input.seeds.assign(24, 0);
  input.seeds[0] = 0xf0;
  input.transmission.assign(24, 1);
  std::vector<uint8_t> light{0xab};
  GpuLightTransportWork work;
  std::string reason;
  {
    Calls refused(Role::LightTransportScratch, 3);
    Require(!TryGpuTransportLight(input, light, work, reason) && reason == "compute_buffer_upload" &&
                light == std::vector<uint8_t>{0xab} && !work.dispatches && !work.scratch_bytes && !refused.dispatches &&
                refused.buffers.Read().namesWithoutObject == 3 && preserved(), "reject partial transport stores before any dispatch");
  }
  TerrainGpuBufferSnapshot transportPeak;
  {
    Calls retry(Role::LightTransportScratch);
    Require(TryGpuTransportLight(input, light, work, reason) && work.dispatches == 15 && work.readbacks == 1 &&
                light.size() == 24 && light[0] == 0xf0 && light[1] == 0xe0 && retry.dispatches == 15 && preserved(),
            "light transport publishes nearby light after retry");
    transportPeak = retry.atFirstDispatch;
    Require(transportPeak.total.objects == 4 && transportPeak.total.bytes == 384 &&
                transportPeak.total.bytes == work.scratch_bytes && retry.buffers.Read().namesWithoutObject == 4,
            "measure actual transient transport extent and retire it before returning");
  }

  const auto retained = Collect();
  const auto live = retained.Read();
  Require(live.total.objects == 7 && extent(live, Role::FluidColumnScan) == columns * (height + 4) &&
              extent(live, Role::TransparentSort) == 65536 && extent(live, Role::BlocklightFlood) == 16 * CHUNK_VOLUME,
          "measure the three retained compute owners through their actual GL names");

  auto *originalContext = glfwGetCurrentContext();
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  auto *sibling = glfwCreateWindow(16, 16, "compute storage context fixture", nullptr, nullptr);
  Require(sibling != nullptr, "create an independent GL context");
  struct RestoreWindow
  {
    GLFWwindow *previous, *sibling;
    ~RestoreWindow()
    {
      if (glfwGetCurrentContext() == sibling)
        ReleasePersistentComputeGpuStorage();
      glfwMakeContextCurrent(previous);
      glfwDestroyWindow(sibling);
    }
  } restoreWindow{originalContext, sibling};
  glfwMakeContextCurrent(sibling);
  Require(!Collect().Read().references && !TryGpuDispatchFluidColumns(flags.data(), height, tops),
          "another current context cannot see or reuse the first context's scratch");
  ReleasePersistentComputeGpuStorage();
  glfwMakeContextCurrent(originalContext);
  Require(retained.Read().total.objects == 7 && preserved(), "foreign teardown preserves the first current context's owners");
  ReleasePersistentComputeGpuStorage();
  const auto retired = retained.Read();
  Require(retired.namesWithoutObject == live.total.objects && !Collect().Read().references && preserved(),
          "normal teardown in the owning context retires all retained compute names");
  glfwMakeContextCurrent(sibling);
  Require(TryGpuDispatchFluidColumns(flags.data(), height, tops) && tops == expected && Collect().Read().total.objects == 2,
          "a new context initializes a fresh scan and returns the same column tops");
  ReleasePersistentComputeGpuStorage();
  glfwMakeContextCurrent(originalContext);
  glUseProgram(GLuint(originalProgram));
  glDeleteBuffers(static_cast<GLsizei>(sentinels.size()), sentinels.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, GLuint(originalBuffer));
  Require(glGetError() == GL_NO_ERROR, "compute lifecycle leaves a healthy context");
  return {{"status", "PASS"}, {"retained", DescribeComputeGpuBuffers(live)},
          {"transport_at_first_dispatch", DescribeComputeGpuBuffers(transportPeak)},
          {"retired_compute_names", retired.namesWithoutObject}, {"fluid_refusal_dispatched", false},
          {"sort_refusal_dispatched", false}, {"transport_refusal_dispatched", false}, {"context_recreation", true}};
}
} // namespace cutum::compute_gpu_storage_fixture
