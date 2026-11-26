#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/OrdinaryNewObservation.h"
#include "Test/WorldOwnedStorageFixture.h"
#include <fstream>
#include <iostream>

int main(int argc,char **argv){
  using namespace cutum;using Json=nlohmann::json;
  const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
  try{
    auto definitions=MakePipelineStorageDefinitions();Json observations=Json::array();size_t prefixes{},checks{};bool queries=true;
    for(int image=0;image<2;++image){UWorldGenContentPinScope pin(WorldOwnedFixtureContent(image));
      for(size_t descriptor=0;descriptor<UWorldGeneratorRegistry::Count();++descriptor){
        UWorld world(nullptr,nullptr);WorldOwnedStorageProbe::Prepare(world,definitions,WorldOwnedFixtureSettings(descriptor));
        WorldOwnedStorageProbe::Rebuild(world);WorldOwnedStorageProbe::Spawn(world);WorldOwnedStorageProbe::Clear(world);
        size_t attempts{},cold{},active{},spawn{},peak{};
        {
          observation::Scope measured;WorldOwnedStorageProbe::Rebuild(world);WorldOwnedStorageProbe::Light(world,3);attempts=observation::State.attempts;
          const auto check=[&]{const auto source=WorldOwnedStorageProbe::Source(world);if(!(source.supported && source.Total()==observation::State.live)){
            std::cerr<<"descriptor="<<descriptor<<" image="<<image<<" model="<<source.Total()<<" actual="<<observation::State.live<<'\n';throw std::runtime_error("world ordinary request equality");}++checks;return source.Total();};
          cold=check();const auto cold_source=WorldOwnedStorageProbe::Source(world);
          const auto source_upper=BoundWorldOwnedStorageTransition(cold_source,WorldOwnedStorageProbe::TransitionInput(world),true);
          const auto before=observation::State.attempts;observation::State.fail_after=before;
          const auto query=WorldOwnedStorageProbe::Source(world);queries=queries && query.supported && before==observation::State.attempts;
          observation::State.fail_after=SIZE_MAX;WorldOwnedStorageProbe::Begin(world);active=check();WorldOwnedStorageProbe::End(world);require(check()==cold,"world cache retirement retains exact source");
          WorldOwnedStorageProbe::Spawn(world);spawn=check();peak=observation::State.peak;require(source_upper.supported && peak<=source_upper.additional_requests,"declared world construction/cache/spawn source upper");
          void *extra=::operator new(73);require(WorldOwnedStorageProbe::Source(world).Total()!=observation::State.live,"world unmodeled source request control");::operator delete(extra);
          WorldOwnedStorageProbe::Clear(world);require(!observation::State.live && !observation::State.blocks,"world selected source destruction0");
        }
        for(size_t prefix=0;prefix<attempts;++prefix){
          observation::Scope measured;WorldOwnedStorageProbe::Rebuild(world);WorldOwnedStorageProbe::Light(world,3);
          const auto old=WorldOwnedStorageProbe::Source(world);auto *identity=WorldOwnedStorageProbe::Generator(world);
          const auto before=observation::State.attempts;observation::State.fail_after=before+prefix;bool caught=false,generator_built=false;
          try{WorldOwnedStorageProbe::Rebuild(world);generator_built=true;WorldOwnedStorageProbe::Light(world,3);}catch(const std::bad_alloc&){caught=true;}
          observation::State.fail_after=SIZE_MAX;
          require(caught,"controlled world source request refused");
          // A final lighting replacement may fail after a successful generator
          // replacement;both states must still have exact,owned source storage.
          const auto after=WorldOwnedStorageProbe::Source(world);require(after.supported && after.Total()==observation::State.live,"world refusal retains exact owned source");
          if(!generator_built)require(WorldOwnedStorageProbe::Generator(world)==identity && after.generation.requests==old.generation.requests,"old generator identity and source preserved on failed construction");
          require(after.lighting==old.lighting && after.lighting_requests==old.lighting_requests,"old lighting survives refused replacement");
          WorldOwnedStorageProbe::Rebuild(world);WorldOwnedStorageProbe::Light(world,3);
          require(WorldOwnedStorageProbe::Source(world).Total()==observation::State.live,"same-input world source retry");
          const auto replacement_upper=BoundWorldOwnedStorageTransition(old,WorldOwnedStorageProbe::TransitionInput(world),true);
          require(replacement_upper.supported && observation::State.peak<=replacement_upper.additional_requests,"old/new world replacement source upper");
          WorldOwnedStorageProbe::Clear(world);require(!observation::State.live && !observation::State.blocks,"world refusal/retry source destruction0");++prefixes;
        }
        observations.push_back({{"descriptor",descriptor},{"image_mode",image},{"construction_attempts",attempts},{"cold",cold},{"active",active},{"spawn",spawn},{"peak",peak}});
      }
    }
    UWorld world(nullptr,nullptr);WorldOwnedStorageProbe::Clear(world);std::array<size_t,3> lighting{};
    for(int kind=1;kind<=3;++kind){observation::Scope measured;WorldOwnedStorageProbe::Light(world,kind);const auto source=WorldOwnedStorageProbe::Source(world);
      require(source.supported && source.lighting_kind==size_t(kind) && source.Total()==observation::State.live,"concrete lighting object/embedded overlap");lighting[kind-1]=source.Total();WorldOwnedStorageProbe::Clear(world);require(!observation::State.live,"lighting retirement0");}
    auto report=RunWorldOwnedStorageProbe();report["ordinary_observations"]=observations;report["ordinary_equality_checks"]=checks;
    report["ordinary_refusal_prefixes"]=prefixes;report["ordinary_query_allocate0"]=queries;report["ordinary_lighting_requests"]=lighting;report["selected_final_live"]=observation::State.live;
    require(queries,"world queries allocate0 under persistent refusal");std::ofstream out(argc>1?argv[1]:"world-owned-storage.json");out<<report.dump(2)<<'\n';require(bool(out),"world report write");std::cout<<report.dump()<<'\n';return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
