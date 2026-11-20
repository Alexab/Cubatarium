#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/RelightInputValidation.h"
#include "World/Lighting/RelightResultInstall.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "Render/GlIncludes.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace cutum;
void Require(bool ok, const char *message)
{ if (!ok) throw std::runtime_error(message); }
void Equal(const RelightComputeResult &cpu, const RelightComputeResult &other)
{
  Require(cpu.inputs_complete && other.inputs_complete &&
              cpu.chunks.size()==other.chunks.size() && !cpu.chunks.empty(),
          "both backends return complete exact write set");
  for(size_t n=0;n<cpu.chunks.size();++n)
    Require(cpu.chunks[n].coord==other.chunks[n].coord &&
                cpu.chunks[n].light_packed==other.chunks[n].light_packed &&
                cpu.chunks[n].padded_light->cells==other.chunks[n].padded_light->cells,
            "all interior and padded bytes match CPU geometry solver");
}
bool Malformed(const LightTransportInput &, std::vector<uint8_t> &out,
               GpuLightTransportWork &work, std::string &)
{ out={255}; work.dispatches=1; work.readbacks=1; return true; }
bool Throwing(const LightTransportInput &, std::vector<uint8_t> &out,
              GpuLightTransportWork &, std::string &)
{ out={255}; throw std::runtime_error("injected transport failure"); }
unsigned BudgetCalls=0;
bool BudgetProbe(const LightTransportInput &, std::vector<uint8_t> &,
                 GpuLightTransportWork &, std::string &)
{ ++BudgetCalls; return false; }

struct Fixture
{
  std::shared_ptr<UBlockDefinitionStorage> definitions=std::make_shared<UBlockDefinitionStorage>();
  UBlockRegistry registry{nullptr,definitions};
  UBlockWorld world;
  RelightJobSpec spec;
  BlockDefinition stone,torch,water;
  const std::vector<glm::ivec3> targets{{-1,-3,-1},{0,-3,-1}};
  Fixture()
  {
    stone.Physics=BlockPhysicsProfile::Solid(); stone.Name="stone";
    torch.Render.Style=BlockRenderStyle::Cross; torch.Name="torch";
    water.Physics.IsLiquid=true; water.Physics.FluidKindPreset=FluidKind::Water; water.Name="water";
    world.SetVerticalProfile({-48,47,-17});
    world.SetFluidDefinitions(definitions.get());
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
    spec.min_world_y=-48; spec.max_world_y=-33;
    spec.include_known_air_outputs=true;
  }
  void SetPhase(int phase)
  {
    torch.Lighting.Emission=phase==2?6:14;
    definitions->ReplaceAll({{8,stone},{9,water},{50,torch}},{{"stone",8},{"water",9},{"torch",50}});
    for(int x=-32;x<32;++x)
      for(int z=-32;z<16;++z)
      {
        world.SetBlock({x,32,z},phase==0?8:BLOCK_AIR);
        for(int y=-35;y<=-33;++y) world.SetBlock({x,y,z},phase<3?9:BLOCK_AIR);
      }
    world.SetBlock({-1,-40,-8},phase==3?BLOCK_AIR:50);
    world.SetBlock({0,-40,-8},phase==2?8:BLOCK_AIR);
    if(phase==3) world.GetChunkManager().RemoveChunk(targets[1]);
    // Hostile old bytes cannot become a solver input on either backend.
    std::array<uint8_t,CHUNK_VOLUME> poison; poison.fill(0x97);
    for(const auto coord:targets) world.GetChunkManager().ReplaceSectionLightData(coord,poison);
  }
  UChunkRelightSnapshot Capture()
  {
    auto snapshot=UChunkRelightSnapshot::CaptureSections(world,spec,targets);
    snapshot.SetInputCatalog(registry.GetDefinitionsCatalogSnapshot());
    return snapshot;
  }
};
void Run(Fixture &fixture)
{
  for(int phase=0;phase<4;++phase)
  {
    fixture.SetPhase(phase);
    auto captured=fixture.Capture(), cpu_snapshot=captured;
    const auto cpu=cpu_snapshot.Compute(fixture.registry);
    const auto gpu=captured.Compute(fixture.registry,TryGpuTransportLight);
    Equal(cpu,gpu);
    Require(gpu.transport==RelightComputeResult::Transport::Gpu &&
                gpu.transport_work.dispatches==15 && gpu.transport_work.readbacks==1,
            "snapshot actually executes GPU transport and readback");
    const auto catalog=fixture.registry.GetDefinitionsCatalogSnapshot();
    Require(ValidateRelightInputs(gpu,fixture.world,catalog,true)==RelightInputValidation::Current,
            "GPU result retains full immutable input validation");
    for(const auto &chunk:gpu.chunks) InstallComputedLight(fixture.world,chunk,true,true);
    Require(fixture.world.RecordLightCalculation(gpu,catalog)==gpu.chunks.size() &&
                fixture.world.HasCompleteLightSamplingCalculation(fixture.targets[1],catalog),
            "GPU padded result installs a current calculation certificate");
    if(phase==3)
      Require(!fixture.world.GetChunkManager().HasChunk(fixture.targets[1]),
              "GPU field-only AIR publication allocates no geometry");
    const auto sample=fixture.world.SampleSceneLight({4,-40,-8});
    Require(sample.packed && UnpackSky(*sample.packed)==(phase==0?0:phase<3?12:15),
            "full-height roof and water context reach negative section");
    if(phase==3)
      for(const auto &chunk:gpu.chunks)
        for(const auto packed:chunk.padded_light->cells)
          Require(UnpackBlock(packed)==0,"GPU removal leaves no stale blocklight");
    auto malformed=fixture.Capture();
    const auto fallback=malformed.Compute(fixture.registry,Malformed);
    Equal(cpu,fallback);
    Require(fallback.transport==RelightComputeResult::Transport::CpuFallback &&
                fallback.transport_fallback_reason=="invalid_or_incomplete_gpu_readback",
            "partial GPU readback is discarded before exact CPU fallback");
    auto throws=fixture.Capture();
    const auto exception_fallback=throws.Compute(fixture.registry,Throwing);
    Equal(cpu,exception_fallback);
    Require(exception_fallback.transport==RelightComputeResult::Transport::CpuFallback &&
                exception_fallback.transport_fallback_reason=="gpu_transport_exception",
            "backend exception cannot publish partial output or lose CPU fallback");
    if(phase==1)
      for(bool sky_only:{false,true})
      {
        fixture.spec.include_skylight=sky_only; fixture.spec.include_block_light=!sky_only;
        auto selected=fixture.Capture(), selected_cpu=selected;
        const auto a=selected_cpu.Compute(fixture.registry);
        const auto b=selected.Compute(fixture.registry,TryGpuTransportLight);
        Equal(a,b);
        Require(b.transport==RelightComputeResult::Transport::Gpu,"selected domain uses real GPU");
      }
    fixture.spec.include_skylight=fixture.spec.include_block_light=true;
    std::cout<<"snapshot_phase="<<phase<<" targets="<<gpu.chunks.size()<<" padded_cells="
             <<gpu.chunks.size()*SectionLightVolume::CellCount<<" GPU PASS\n";
  }
  Fixture clipped;
  clipped.SetPhase(1);
  clipped.world.SetVerticalProfile({-45,39,-17});
  auto clipped_gpu=clipped.Capture(), clipped_cpu=clipped_gpu;
  const auto edge=clipped_gpu.Compute(clipped.registry,TryGpuTransportLight);
  Equal(clipped_cpu.Compute(clipped.registry),edge);
  Require(edge.transport==RelightComputeResult::Transport::Gpu,
          "partially clipped negative section uses GPU and exact profile boundary");
  Fixture sparse;
  sparse.world.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
  sparse.definitions->ReplaceAll({},{});
  auto budget_gpu=UChunkRelightSnapshot::CaptureSections(sparse.world,sparse.spec,
      {{-1000,-3,0},{1000,-3,0}}), budget_cpu=budget_gpu;
  const auto budget=budget_gpu.Compute(sparse.registry,BudgetProbe);
  Equal(budget_cpu.Compute(sparse.registry),budget);
  Require(BudgetCalls==0 && budget.transport==RelightComputeResult::Transport::CpuFallback &&
              budget.transport_fallback_reason=="snapshot_dense_domain_over_budget",
          "sparse over-budget bounding box falls back before allocation/dispatch");
  std::cout<<"snapshot_edges=field_only_air,partial_profile,dense_cap,backend_exception PASS\n";
}
}
int main()
{
  Fixture fixture; fixture.SetPhase(0);
  auto initial=fixture.Capture(), cpu_snapshot=initial;
  const auto cpu=cpu_snapshot.Compute(fixture.registry);
  const auto no_context=initial.Compute(fixture.registry,TryGpuTransportLight);
  try
  {
    Equal(cpu,no_context);
    Require(no_context.transport==RelightComputeResult::Transport::CpuFallback &&
                no_context.transport_work.dispatches==0 && no_context.transport_work.readbacks==0,
            "missing GL context runs exact CPU fallback without GPU success");
  }
  catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
  if(!glfwInit()) return 77;
  glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3); glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
  auto *window=glfwCreateWindow(32,32,"GPU snapshot",nullptr,nullptr);
  if(!window) { glfwTerminate(); return 77; }
  glfwMakeContextCurrent(window); glewExperimental=GL_TRUE;
  if(glewInit()!=GLEW_OK) { glfwDestroyWindow(window); glfwTerminate(); return 77; }
  while(glGetError()!=GL_NO_ERROR) {}
  std::cout<<"GPU: "<<glGetString(GL_RENDERER)<<" GL: "<<glGetString(GL_VERSION)<<'\n';
  int result=0;
  try { Run(fixture); Require(glGetError()==GL_NO_ERROR,"snapshot driver GL state clean"); }
  catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; result=1; }
  glfwDestroyWindow(window); glfwTerminate(); return result;
}
