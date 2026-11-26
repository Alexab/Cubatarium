#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/OrdinaryNewObservation.h"
#include "Test/PipelineRetainedStorageFixture.h"
#include <fstream>
#include <iostream>

int main(int argc,char **argv){
  using namespace cutum;using Json=nlohmann::json;
  const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
  try {
    const auto layout=UComposableWorldGenerator::GetRetainedStorageLayout();
    require(layout.supported,"MSVC Release source layout");
    auto definitions=MakePipelineStorageDefinitions();UBlockRegistry registry(nullptr,definitions);auto content=CaptureWorldGenContentSnapshot();
    Json observations=Json::array();size_t refusal_prefixes=0;
    for(int image=0;image<2;++image){
      auto pack=std::make_shared<WorldGenPack>();pack->BiomeMode=image?WorldGenBiomeMode::Image:WorldGenBiomeMode{};
      auto captured=content;captured.Pack=pack;UWorldGenContentPinScope pin(std::move(captured));
      for(size_t descriptor=0;descriptor<UWorldGeneratorRegistry::Count();++descriptor){
        UBlockWorld world;ProceduralSettings settings;UWorldGeneratorRegistry::Get(descriptor).ApplyDefaults(settings);
        settings.MinHeight=0;settings.MaxHeight=16;settings.FillWater=false;
        settings.SeedText=std::string(4096,'s');settings.SeedText.reserve(32768);
        settings.WorldGenPackId=std::string(3072,'p');settings.WorldGenPresetId=std::string(2048,'r');
        const std::string owner(1536,'o');
        const auto build=[&]{WorldGenContext ctx(world,registry,settings);ctx.WorldgenOwnerPackId=owner;return UWorldGeneratorRegistry::Create(std::move(ctx));};
        size_t attempts{},peak{},retained{},active{},spawn{},cold_actual{},active_actual{},spawn_actual{},bound_upper{};bool exact=true,query_free=true,negative=false,final0=false;
        {
          observation::Scope measured;auto pipeline=build();attempts=observation::State.attempts;
          const auto cold=pipeline->InspectRetainedStorage(true);retained=PipelineRequestSum(cold);
          cold_actual=observation::State.live;
          exact=exact && cold.supported && retained==observation::State.live;
          const auto before_query=observation::State.attempts;observation::State.fail_after=before_query;
          const auto query=pipeline->InspectRetainedStorage(true);
          query_free=query.supported && before_query==observation::State.attempts;observation::State.fail_after=SIZE_MAX;
          auto *actual=static_cast<UComposableWorldGenerator*>(pipeline.get());
          actual->BeginChunkCoarseCache(0,0,24);const auto cached=pipeline->InspectRetainedStorage(true);active=PipelineRequestSum(cached);
          active_actual=observation::State.live;
          exact=exact && cached.supported && active==observation::State.live;
          actual->EndChunkCoarseCache();exact=exact && pipeline->InspectRetainedStorage(true).requests==cold.requests && observation::State.live==retained;
          glm::vec3 result{};pipeline->TickSpawnResolve(world,registry,0,0,0,result);const auto populated=pipeline->InspectRetainedStorage(true);spawn=PipelineRequestSum(populated);
          spawn_actual=observation::State.live;
          exact=exact && populated.supported && spawn==observation::State.live;
          void *unmodeled=::operator new(73);negative=observation::State.live!=spawn;::operator delete(unmodeled);
          const auto bound=BoundPipelineStorageTransition(cold,{RuntimeSettingsTextCapacityBytes(settings),ExternalStringCapacityBytes(owner),true,false,true,false});
          peak=observation::State.peak;exact=exact && bound.supported && peak<=bound.additional_requests;
          bound_upper=bound.additional_requests;
          pipeline.reset();final0=!observation::State.live && !observation::State.blocks;
        }
        if(!(exact && query_free && negative && final0)){
          std::cerr<<Json{{"descriptor",descriptor},{"image",image},{"model",{retained,active,spawn}},{"actual",{cold_actual,active_actual,spawn_actual}},
            {"peak",peak},{"upper",bound_upper},{"query_free",query_free},{"negative",negative},{"final0",final0}}.dump()<<std::endl;
          throw std::runtime_error("pipeline retained request diagnostic");
        }
        for(size_t prefix=0;prefix<attempts;++prefix){bool threw=false,clean=false;
          {observation::Scope measured(prefix);try{auto pipeline=build();}catch(const std::bad_alloc &){threw=true;}
            clean=!observation::State.live && !observation::State.blocks;}
          require(threw && clean,"pipeline constructor refusal unwinds all requests");
          auto retry=build();require(retry->InspectRetainedStorage(true).supported,"same-input pipeline construction retry");++refusal_prefixes;
        }
        observations.push_back({{"descriptor",descriptor},{"image_mode",image},{"construction_attempts",attempts},{"retained",retained},
          {"active",active},{"spawn",spawn},{"peak",peak},{"query_allocations",0},{"final_live",0}});
      }
    }
    size_t world_attempts{},world_requests{};bool world_exact=false,world_final=false;
    {
      observation::Scope measured;auto world=std::make_unique<UBlockWorld>();world_attempts=observation::State.attempts;
      const auto source=world->InspectPipelineScratchStorage(true);
      world_requests=sizeof(UBlockWorld)+source.metadata+source.memo+source.controls+layout.widths[11];
      world_exact=source.supported && observation::State.live==world_requests;
      const auto attempts=observation::State.attempts;observation::State.fail_after=attempts;
      world_exact=world_exact && world->InspectPipelineScratchStorage(true).supported && observation::State.attempts==attempts;
      observation::State.fail_after=SIZE_MAX;world.reset();world_final=!observation::State.live && !observation::State.blocks;
    }
    require(world_exact && world_final,"empty scratch world source model includes all ordinary requests");
    for(size_t prefix=0;prefix<world_attempts;++prefix){bool threw=false,clean=false;
      {observation::Scope measured(prefix);try{auto world=std::make_unique<UBlockWorld>();}catch(const std::bad_alloc &){threw=true;}
        clean=!observation::State.live && !observation::State.blocks;}
      require(threw && clean,"scratch world constructor refusal unwinds to zero");auto retry=std::make_unique<UBlockWorld>();
      require(retry->InspectPipelineScratchStorage(true).supported,"same-input scratch construction retry");++refusal_prefixes;
    }
    PipelineStorageSnapshot fresh;bool passive=false;
    std::thread first([&]{observation::Scope refused(0);fresh=InspectCallingThreadPipelineStorage();passive=fresh.supported && !fresh.initialized && !observation::State.attempts;});first.join();
    require(passive,"fresh calling-thread provider is allocation-free and constructs no world");
    auto report=RunPipelineRetainedStorageProbe();report["ordinary_requests"]=observations;report["world_ordinary_requests"]=world_requests;
    report["persistent_refusal_prefixes"]=refusal_prefixes;report["ordinary_queries_allocate0"]=true;report["unmodeled_request_control"]=true;
    std::ofstream out(argc>1?argv[1]:"pipeline-retained-storage.json");out<<report.dump(2)<<'\n';require(bool(out),"report write");
    std::cout<<report.dump()<<std::endl;return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
}
