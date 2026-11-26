#pragma once
#include "World/Core/World.h"
#include "World/Core/WorldOwnedCallbacks.h"
#include "World/Core/WorldOwnedStorageTransition.h"
#include "World/Lighting/LightingPipelineFactory.h"
#include "World/Lighting/FullLightingPipeline.h"
#include "Test/PipelineRetainedStorageFixture.h"

namespace cutum {
// Friend fixture uses actual UWorld paths and factory-owned members. No world,
// generator,callback,lighting or filesystem implementation is replaced.
struct WorldOwnedStorageProbe {
  static void Prepare(UWorld &world,const std::shared_ptr<UBlockDefinitionStorage> &definitions,ProceduralSettings settings){
    world.BlockDefinitions=definitions;world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
    world.ProceduralTemplate=std::move(settings);world.WorldgenOwnerPackId=std::string(1536,'o');
    world.LightingPipeline.reset();
    world.RebuildWorldGenPipeline();
  }
  static void Rebuild(UWorld &world){world.RebuildWorldGenPipelineFromResolvedSources();}
  static void Clear(UWorld &world){world.WorldGen.reset();world.LightingPipeline.reset();}
  static PipelineTransitionDomain TransitionInput(const UWorld &world){return {RuntimeSettingsTextCapacityBytes(world.ProceduralTemplate),ExternalStringCapacityBytes(world.WorldgenOwnerPackId),true,false,true,false};}
  static IUWorldGenPipeline *Generator(UWorld &world){return world.WorldGen.get();}
  static void Light(UWorld &world,int kind){
    if(kind==2)world.LightingPipeline=std::make_unique<UFullLightingPipeline>();
    else world.LightingPipeline=ULightingPipelineFactory::Create(kind==1?LightingMode::Flat:LightingMode::Full);
  }
  static WorldOwnedStorageSnapshot Source(const UWorld &world){return world.InspectOwnedGeneratorsAndCallbacks(nullptr,nullptr,true);}
  static WorldGenContext &Context(UWorld &world){return static_cast<UComposableWorldGenerator*>(world.WorldGen.get())->GetContext();}
  static void Begin(UWorld &world){static_cast<UComposableWorldGenerator*>(world.WorldGen.get())->BeginChunkCoarseCache(0,0,24);}
  static void End(UWorld &world){static_cast<UComposableWorldGenerator*>(world.WorldGen.get())->EndChunkCoarseCache();}
  static void Spawn(UWorld &world){glm::vec3 result{};world.WorldGen->TickSpawnResolve(world.BlockWorld,*world.BlockRegistry,0,0,0,result);}
  static void InvokeDirtyWithoutWork(UWorld &world){world.CooperativeBulkGenerating=true;Context(world).OnColumnMeshDirty(0,0,0,15);world.CooperativeBulkGenerating=false;}
  static bool ContextControls(UWorld &world){
    auto &ctx=Context(world);ObjectFeatureConfig foreign_features;
    const auto *features=ctx.ObjectFeatures;ctx.ObjectFeatures=&foreign_features;const bool foreign_feature=!Source(world).supported;ctx.ObjectFeatures=features;
    auto original=std::move(world.BlockRegistry);world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,world.BlockDefinitions);
    const bool foreign_registry=!Source(world).supported;world.BlockRegistry=std::move(original);
    size_t calls{};world.SetOnBlockRegistryChanged([padding=std::array<char,1024>{},&calls](){++calls;(void)padding;});
    const bool unknown_callback=!Source(world).supported && !calls;world.SetOnBlockRegistryChanged({});
    world.SetOnAfterWorldDataLoaded(CoreAfterWorldLoadedCallback{nullptr});
    const bool null_owner=!Source(world).supported;world.SetOnAfterWorldDataLoaded({});
    world.BlockWorld.GetChunkManager().SetBeforeChunkAllocationFn([&](){++calls;});
    const bool foreign_hook=!Source(world).supported && !calls;world.BlockWorld.GetChunkManager().SetBeforeChunkAllocationFn({});
    return foreign_feature && foreign_registry && unknown_callback && null_owner && foreign_hook && Source(world).supported;
  }
};
inline ProceduralSettings WorldOwnedFixtureSettings(size_t descriptor){
  ProceduralSettings settings;UWorldGeneratorRegistry::Get(descriptor).ApplyDefaults(settings);
  settings.MinHeight=0;settings.MaxHeight=16;settings.FillWater=false;
  settings.SeedText=std::string(4096,'s');settings.WorldGenPackId=std::string(3072,'p');settings.WorldGenPresetId=std::string(2048,'r');return settings;
}
inline WorldGenContentSnapshot WorldOwnedFixtureContent(int image){
  auto content=CaptureWorldGenContentSnapshot();auto pack=std::make_shared<WorldGenPack>();
  pack->BiomeMode=image?WorldGenBiomeMode::Image:WorldGenBiomeMode{};content.Pack=pack;
  content.Features=std::make_shared<ObjectFeatureConfig>();return content;
}
inline nlohmann::json RunWorldOwnedStorageProbe(){
  using Json=nlohmann::json;const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
  const auto layout=GetWorldOwnedStorageLayout();auto definitions=MakePipelineStorageDefinitions();Json cases=Json::array();
  for(int image=0;image<2;++image){UWorldGenContentPinScope pin(WorldOwnedFixtureContent(image));
    for(size_t descriptor=0;descriptor<UWorldGeneratorRegistry::Count();++descriptor){
      UWorld world(nullptr,nullptr);WorldOwnedStorageProbe::Prepare(world,definitions,WorldOwnedFixtureSettings(descriptor));
      WorldOwnedStorageProbe::Rebuild(world);WorldOwnedStorageProbe::Light(world,3);const auto cold=WorldOwnedStorageProbe::Source(world);
      require(cold.supported && cold.borrowed_context_matches,"actual world source profile");
      const auto input=WorldOwnedStorageProbe::TransitionInput(world);const auto upper=BoundWorldOwnedStorageTransition(cold,input,true);
      require(upper.supported && !BoundWorldOwnedStorageTransition(cold,input,false).supported,"world pinned factory source history required");
      require(!WorldOwnedStorageProbe::Generator(world)->InspectRetainedStorage(true).supported,"W10 TLS profile still rejects world callback history");
      WorldOwnedStorageProbe::InvokeDirtyWithoutWork(world);WorldOwnedStorageProbe::Begin(world);const auto active=WorldOwnedStorageProbe::Source(world);
      WorldOwnedStorageProbe::End(world);require(WorldOwnedStorageProbe::Source(world).generation.requests==cold.generation.requests,"world cache retirement exact");
      WorldOwnedStorageProbe::Spawn(world);const auto spawn=WorldOwnedStorageProbe::Source(world);
      require(active.supported && spawn.supported && spawn.generation.requests[4]>0 && WorldOwnedStorageProbe::ContextControls(world),"world active/spawn/context/callback controls");
      std::array<size_t,10> captures{},requests{};for(size_t i=0;i<captures.size();++i){captures[i]=cold.callbacks[i].capture_inline;requests[i]=cold.callbacks[i].requests;require(!requests[i],"current named raw-owner callbacks are inline");}
      cases.push_back({{"descriptor",descriptor},{"image_mode",image},{"cold",cold.generation.requests},{"active",active.generation.requests},{"spawn",spawn.generation.requests},
        {"callback_capture_inline",captures},{"callback_requests",requests},{"lighting_requests",cold.lighting_requests},{"lighting_kind",cold.lighting_kind},{"transition_upper",upper.additional_requests}});
    }
  }
  UWorld world(nullptr,nullptr),second(nullptr,nullptr);const auto first=WorldOwnedStorageProbe::Source(world),other=WorldOwnedStorageProbe::Source(second);
  require(first.supported && other.supported,"fresh world query constructs no generator");
  std::vector<const WorldOwnedStorageSnapshot*> views{&first,&other};const auto joined=JoinWorldOwnedStorage(views,2,true,true);
  auto aliases=views;for(int i=0;i<64;++i)aliases.push_back(&first);const auto repeated=JoinWorldOwnedStorage(aliases,2,true,true);
  auto conflict=first;conflict.lighting_requests++;std::vector<const WorldOwnedStorageSnapshot*> bad{&first,&conflict,&other};
  auto missing=views;missing.pop_back();auto huge=first;huge.generation.requests[0]=SIZE_MAX;
  std::vector<const WorldOwnedStorageSnapshot*> overflowing{&huge,&other};
  const bool controls=joined.supported && joined.worlds==2 && repeated.supported && repeated.requested==joined.requested &&
    !JoinWorldOwnedStorage(views,2,false,true).supported && !JoinWorldOwnedStorage(views,2,true,false).supported &&
    !JoinWorldOwnedStorage(missing,2,true,true).supported && !JoinWorldOwnedStorage(bad,2,true,true).supported &&
    JoinWorldOwnedStorage(overflowing,2,true,true).overflow && !world.InspectOwnedGeneratorsAndCallbacks(nullptr,nullptr,false).supported;
  require(controls,"world identity/domain/coverage/missing/alias/overflow controls");
  return {{"schema","world-owned-storage.v1"},{"status","PASS"},{"generation_layout",layout.generation.widths},{"lighting_layout",layout.lighting},
    {"parent_inline",{layout.world_inline,layout.block_world_inline,layout.function_inline}},{"cases",cases},{"fresh_query_no_generator",true},
    {"raw_owner_callbacks_only",true},{"context_and_unknown_callback_controls",true},{"identity_domain_coverage_controls",controls},
    {"scope","Actual private world generation/lighting factories and ten stored callbacks. Whole generator and GPU lighting parent once;parent world/BlockWorld/function shells,borrowed resources,other callbacks/threads/CRT/GPU/process remain separately supplied source terms."}};
}
} // namespace cutum
