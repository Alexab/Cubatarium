#pragma once
#include "Test/FluidTestHelpers.h"
#include "WorldGen/Core/WorldGeneratorDescriptor.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Core/RuntimeSettingsText.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

namespace cutum {
inline std::shared_ptr<UBlockDefinitionStorage> MakePipelineStorageDefinitions(){
  auto definitions=std::make_shared<UBlockDefinitionStorage>();
  std::unordered_map<BlockId,BlockDefinition> values;std::unordered_map<std::string,BlockId> names;BlockId next=8;
  for(const char *name:{"stone","water","bedrock","dirt","grass","sand","sandstone","wood","gravel","lava","fire","ore_coal","ore_iron","snow","clay","ice","hellrock"}){
    BlockDefinition value;value.Name=name;value.Physics=BlockPhysicsProfile::Solid();values.emplace(next,value);names.emplace(name,next++);}
  names.emplace("grass_block",names.at("grass"));definitions->ReplaceAll(std::move(values),std::move(names));return definitions;
}
inline size_t PipelineRequestSum(const PipelineStorageSnapshot &source){PipelineStorageEnvelope out;out.supported=true;AddPipelineStorage(out,source,false);return out.additional_requests;}
inline nlohmann::json RunPipelineRetainedStorageProbe(){
  using Json=nlohmann::json;const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
  const auto layout=UComposableWorldGenerator::GetRetainedStorageLayout();
  if(!layout.supported)return {{"schema","pipeline-retained-storage.v1"},{"status","UNSUPPORTED"}};
  auto definitions=MakePipelineStorageDefinitions();UBlockRegistry registry(nullptr,definitions);
  Json cases=Json::array();size_t max_requests=0;
  auto content=CaptureWorldGenContentSnapshot();
  for(int image=0;image<2;++image){
    auto pack=std::make_shared<WorldGenPack>();pack->BiomeMode=image?WorldGenBiomeMode::Image:WorldGenBiomeMode{};
    auto pin_content=content;pin_content.Pack=pack;UWorldGenContentPinScope pin(std::move(pin_content));
    for(size_t i=0;i<UWorldGeneratorRegistry::Count();++i){
      UBlockWorld world;ProceduralSettings settings;UWorldGeneratorRegistry::Get(i).ApplyDefaults(settings);
      settings.MinHeight=0;settings.MaxHeight=16;settings.FillWater=false;
      settings.SeedText=std::string(4096,'s');settings.SeedText.reserve(32768);
      settings.WorldGenPackId=std::string(3072,'p');settings.WorldGenPresetId=std::string(2048,'r');
      WorldGenContext context(world,registry,settings);context.WorldgenOwnerPackId=std::string(1536,'o');
      const auto owner_text=ExternalStringCapacityBytes(context.WorldgenOwnerPackId);
      auto pipeline=UWorldGeneratorRegistry::Create(std::move(context));auto *actual=dynamic_cast<UComposableWorldGenerator*>(pipeline.get());
      require(actual!=nullptr,"all real descriptor factories use known concrete generator");
      const auto cold=pipeline->InspectRetainedStorage(true);require(cold.supported && cold.idle,"known cold pipeline storage");
      const auto before_callbacks=actual->SurfaceYAt(0,0);
      actual->BeginChunkCoarseCache(0,0,24);const auto active=pipeline->InspectRetainedStorage(true);
      require(active.supported,"known active cache/callback shape");
      actual->EndChunkCoarseCache();const auto idle=pipeline->InspectRetainedStorage(true);
      require(idle.supported && idle.idle && idle.requests==cold.requests && actual->SurfaceYAt(0,0)==before_callbacks,"cache retirement restores exact requests and surface output");
      glm::vec3 spawn{};pipeline->TickSpawnResolve(world,registry,0,0,0,spawn);
      const auto candidates=pipeline->InspectRetainedStorage(true);
      require(candidates.supported && candidates.requests[4]>0,"actual cooperative spawn retains candidate backing");
      auto domain=PipelineTransitionDomain{RuntimeSettingsTextCapacityBytes(settings),owner_text,true,false,true,false};
      const auto peak=BoundPipelineStorageTransition(candidates,domain);
      auto missing=domain;missing.source_history_known=false;auto unknown=domain;unknown.coverage_known=false;
      auto huge=domain;huge.fresh_settings_text_upper=SIZE_MAX;
      require(peak.supported && !BoundPipelineStorageTransition(candidates,missing).supported &&
        !BoundPipelineStorageTransition(candidates,unknown).supported && BoundPipelineStorageTransition(candidates,huge).overflow,"history/coverage/overflow controls");
      actual->GetContext().CoarseSurfaceYAt=[padding=std::array<char,1024>{}](int,int){return int(padding[0]);};
      require(!pipeline->InspectRetainedStorage(true).supported,"unknown callback rejected without invocation");
      max_requests=std::max(max_requests,PipelineRequestSum(candidates));
      cases.push_back({{"descriptor",i},{"image_mode",image},{"cold",cold.requests},{"active",active.requests},
        {"idle",idle.requests},{"spawn",candidates.requests},{"transition_upper",peak.additional_requests}});
    }
  }
  PipelineStorageSnapshot fresh;
  std::thread never_initialized([&]{fresh=InspectCallingThreadPipelineStorage();});never_initialized.join();
  require(fresh.supported && !fresh.initialized && !fresh.world && !PipelineRequestSum(fresh),"fresh passive query creates no world");
  struct ForeignPipeline final:UComposableWorldGenerator {using UComposableWorldGenerator::UComposableWorldGenerator;};
  UBlockWorld foreign_world;ForeignPipeline foreign(WorldGenContext(foreign_world,registry,{}),{});
  require(!foreign.InspectRetainedStorage(true).supported,"unknown derived generator must not use the concrete object width");
  UComposableWorldGenerator stack_pipeline(WorldGenContext(foreign_world,registry,{}),{});
  require(!stack_pipeline.InspectRetainedStorage(false).supported,"unknown object allocation origin rejected");
  UBlockWorld empty;const auto world=empty.InspectPipelineScratchStorage(true);
  if(!(world.supported && world.metadata==layout.widths[8] && world.memo==layout.widths[9] && world.controls==layout.widths[10]))
    throw std::runtime_error(Json{{"empty_world_supported",world.supported},{"actual",{world.metadata,world.memo,world.controls}},
      {"layout",{layout.widths[8],layout.widths[9],layout.widths[10]}}}.dump());
  require(world.supported && world.metadata==layout.widths[8] && world.memo==layout.widths[9] && world.controls==layout.widths[10],"empty world source widths match live requests");
  require(!empty.InspectPipelineScratchStorage(false).supported,"unknown scratch factory profile rejected");
  size_t callback_calls=0;empty.SetKnownOpenSkyAbsentQuery([&](glm::ivec3){++callback_calls;return true;});
  require(!empty.InspectPipelineScratchStorage(true).supported && !callback_calls,"foreign world callback is not queried");
  empty.SetKnownOpenSkyAbsentQuery({});empty.SetBlock({0,0,0},8);
  require(!empty.InspectPipelineScratchStorage(true).supported,"nonempty scratch world needs active source view");
  return {{"schema","pipeline-retained-storage.v1"},{"status","PASS"},{"layout",layout.widths},{"inline_children",layout.inline_children},
    {"spawn_raw_upper",layout.spawn_raw_upper},{"cases",cases},{"max_retained_requests",max_requests},{"fresh_query_no_world",true},
    {"empty_world_requests",std::array<size_t,3>{world.metadata,world.memo,world.controls}},
    {"history_callback_world_overflow_controls",true},
    {"scope","Known factory pipeline storage and default empty scratch world;capacity-aware requests,whole parent overlap and explicit construction history. Other worlds/callback histories/platform/process remain outside this source profile."}};
}
}
