#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/RootSourceGlContext.h"
#include "Test/OrdinaryNewObservation.h"
#include "Test/ApplicationOperationStorageFixture.h"
#include <fstream>
#include <iostream>
namespace {
// Independent original capture declarations preserve the pre-change ABI and
// copy/move traits;these lambdas never run and contain no production logic.
void CheckOriginalCaptureTypes(cutum::UNewWorldScreen *owner,const cutum::WorldRunnerRequest &input){
  const cutum::ProceduralSettings settings=input.settings;auto packs=input.packs;const auto view=input.view;const auto gameMode=input.gameMode;const auto difficulty=input.difficulty;
  const auto create=[owner,settings,packs,view,gameMode,difficulty](){};
  const std::string worldName=input.worldName;cutum::ULoadWorldScreen *load_owner=nullptr;const auto load=[load_owner,worldName](){};
  static_assert(sizeof(create)==sizeof(cutum::CreateWorldMenuAction));static_assert(sizeof(load)==sizeof(cutum::LoadWorldMenuAction));
  static_assert(std::is_nothrow_move_constructible_v<decltype(create)> ==std::is_nothrow_move_constructible_v<cutum::CreateWorldMenuAction>);
  static_assert(std::is_nothrow_move_constructible_v<decltype(load)> ==std::is_nothrow_move_constructible_v<cutum::LoadWorldMenuAction>);
}
}
int main(int argc,char **argv){
  using namespace cutum;using Json=nlohmann::json;const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
  try{
    RootSourceGlContext gl;UWorld world(nullptr,nullptr);UCore core(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);UApplication application(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);
    UNewWorldScreen create(&application);ULoadWorldScreen load(&application);auto report=RunApplicationOperationStorageProbe(application,core,world);Json observations=Json::array();size_t prefixes{},checks{};
    CheckOriginalCaptureTypes(&create,ApplicationOperationFixtureRequest(2));
    for(int variant=0;variant<3;++variant){const auto input=ApplicationOperationFixtureRequest(variant);
      for(size_t kind=1;kind<=6;++kind){
        auto prepared=MakeApplicationActionFixture(kind,application,create,load,input); // stable source input,not measured again
        size_t attempts{},selected{};
        {observation::Scope epoch;std::function<void()> fn=prepared;const auto s=InspectApplicationAction(fn,&application,&create,&load,true);attempts=observation::State.attempts;selected=s.Total();require(s.Supported() && selected==observation::State.live,"actual callback copy ordinary request equality");++checks;
          const auto before=observation::State.attempts;observation::State.fail_after=before;const auto query=InspectApplicationAction(fn,&application,&create,&load,true);
          require(query.Supported() && query.Total()==selected && observation::State.attempts==before,"action query allocate0 under persistent refusal");observation::State.fail_after=SIZE_MAX;
          void *extra=::operator new(73);require(InspectApplicationAction(fn,&application,&create,&load,true).Total()!=observation::State.live,"unmodeled callback allocation control");::operator delete(extra);
          std::function<void()> executing=std::move(fn);require(!fn && InspectApplicationAction(executing,&application,&create,&load,true).Total()==observation::State.live,"member-to-execution move retains exact owned source");executing={};require(!observation::State.live && !observation::State.blocks,"action retirement0");}
        for(size_t prefix=0;prefix<attempts;++prefix){observation::Scope epoch;std::function<void()> destination=prepared;const auto old=InspectApplicationAction(destination,&application,&create,&load,true);const auto before=observation::State.attempts;observation::State.fail_after=before+prefix;bool refused=false;
          try{destination=prepared;}catch(const std::bad_alloc&){refused=true;}observation::State.fail_after=SIZE_MAX;
          const auto after=InspectApplicationAction(destination,&application,&create,&load,true);require(refused && after.Supported() && after.target==old.target && after.Total()==old.Total() && after.Total()==observation::State.live,"refused callback replacement preserves exact old source");
          destination=prepared;require(InspectApplicationAction(destination,&application,&create,&load,true).Total()==observation::State.live,"same-input callback retry");destination={};require(!observation::State.live && !observation::State.blocks,"callback refusal/retry retirement0");++prefixes;}
        observations.push_back({{"variant",variant},{"kind",kind},{"attempts",attempts},{"selected",selected}});
      }
      auto warm=std::make_unique<UWorldOperationRunner>(core,world);warm->Start(input);warm.reset();size_t attempts{};
      {observation::Scope epoch;auto runner=std::make_unique<UWorldOperationRunner>(core,world);const auto cold=runner->InspectOwnedStorage(&core,&world,true);require(cold.Total()==observation::State.live,"runner cold equality");++checks;
        runner->Start(input);attempts=observation::State.attempts;require(runner->InspectOwnedStorage(&core,&world,true).Total()==observation::State.live,"runner request equality");++checks;
        ApplicationOperationSourceProbe::PendingName(*runner,std::string(9000,'n'));ApplicationOperationSourceProbe::Fail(*runner,"Application source fixture");require(runner->InspectOwnedStorage(&core,&world,true).Total()==observation::State.live,"runner pending name/error equality");++checks;
        runner->Start(ApplicationOperationFixtureRequest(0));const auto retained=runner->InspectOwnedStorage(&core,&world,true);require(retained.supported && retained.Total()==observation::State.live && retained.requests[5]>0,"runner retains cleared buffer backing");++checks;
        const auto before=observation::State.attempts;observation::State.fail_after=before;const auto source=runner->InspectOwnedStorage(&core,&world,true);require(source.supported && before==observation::State.attempts,"runner query allocate0");observation::State.fail_after=SIZE_MAX;runner.reset();require(!observation::State.live && !observation::State.blocks,"runner selected retirement0");}
      for(size_t prefix=0;prefix<attempts;++prefix){observation::Scope epoch(prefix);bool refused=false;try{auto runner=std::make_unique<UWorldOperationRunner>(core,world);runner->Start(input);}catch(const std::bad_alloc&){refused=true;}
        observation::State.fail_after=SIZE_MAX;require(refused && !observation::State.live && !observation::State.blocks,"runner constructor/request refusal unwinds0");auto retry=std::make_unique<UWorldOperationRunner>(core,world);retry->Start(input);require(retry->InspectOwnedStorage(&core,&world,true).Total()==observation::State.live,"same-input runner retry equality");retry.reset();require(!observation::State.live && !observation::State.blocks,"runner refusal/retry retirement0");++prefixes;}
    }
    bool slot_cleared=false,successor_held=false;size_t invoked{};
    ApplicationOperationSourceProbe::SetPending(application,[&](){AllocatorSourcePhase phase(NewRootStorageEpoch(),1);slot_cleared=application.InspectOperationStorage(phase,true).actions[0].kind==ApplicationActionKind::Empty;++invoked;ApplicationOperationSourceProbe::SetPending(application,[&](){++invoked;});});
    ApplicationOperationSourceProbe::ExecutePending(application);AllocatorSourcePhase phase(NewRootStorageEpoch(),1);successor_held=application.InspectOperationStorage(phase,true).actions[0].callback.present;
    ApplicationOperationSourceProbe::ExecutePending(application);require(slot_cleared && successor_held && invoked==2,"actual move-before-invoke and newly scheduled successor preserved");
    for(size_t kind=1;kind<=4;++kind){ApplicationOperationSourceProbe::SetPending(application,MakeApplicationActionFixture(kind,application,create,load,ApplicationOperationFixtureRequest(0)));const auto source=application.InspectOperationStorage(phase,true);
      require(source.supported && static_cast<size_t>(source.actions[0].kind)==kind && application.InspectStoredActionCallbacks(true).rows[33].supported,"actual active application member source");}
    ApplicationOperationSourceProbe::Clear(application);phase.Close();require(!application.InspectOperationStorage(phase,true).supported,"actual closed application source phase");
    report["ordinary_cases"]=observations;report["ordinary_equality_checks"]=checks;report["refusal_prefixes"]=prefixes;report["query_allocate0"]=true;report["move_before_invoke_successor"]=true;report["selected_final_live"]=observation::State.live;
    std::ofstream file(argc>1?argv[1]:"application-operation-storage.json");file<<report.dump(2)<<'\n';require(bool(file),"application operation report write");std::cout<<report.dump()<<'\n';return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
