#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/OrdinaryNewObservation.h"
#include "Test/RuntimeNameStorageFixture.h"
#include <fstream>
#include <iostream>

int main(int argc,char **argv){
  using namespace cutum;using Json=nlohmann::json;
  const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
  try {
    RuntimeNameStorageProbe::Inputs inputs;RuntimeNameStorageProbe::ScopedFlag truth(inputs.refused_flag);
    std::exception_ptr error;bool exact=true,query_free=true,clean_failures=true,unmodeled=false;size_t peak{},retained{},steps{},failure_prefixes{};
    std::thread owner([&]{try{
      observation::Scope observed;
      const auto check=[&]{const auto source=InspectCallingThreadRuntimeNames();
        if(!(source.supported && source.Total()==observation::State.live))throw std::runtime_error("name storage request equality");++steps;};
      check();
      for(size_t i=0;i<140;++i){const auto before=InspectCallingThreadRuntimeNames();const auto upper=BoundEnvironmentFlagInsertion(before,inputs.flags[i].size(),true,true);
        observation::State.peak=observation::State.live;const bool expected=IsEnvironmentFlagEnabledUncached(inputs.flags[i].c_str());
        exact=exact && IsEnvironmentFlagEnabled(inputs.flags[i].c_str())==expected && observation::State.peak<=upper.requested;check();
      }
      size_t index=140;while(InspectCallingThreadRuntimeNames().flags<InspectCallingThreadRuntimeNames().flag_capacity){
        require(index<inputs.flags.size(),"bounded vector-full setup");(void)IsEnvironmentFlagEnabled(inputs.flags[index++].c_str());check();}
      for(size_t prefix=0;prefix<2;++prefix){const auto before=InspectCallingThreadRuntimeNames();const auto attempts=observation::State.attempts;
        observation::State.fail_after=attempts+prefix;const bool value=IsEnvironmentFlagEnabled(inputs.refused_flag.c_str());
        observation::State.fail_after=SIZE_MAX;const auto after=InspectCallingThreadRuntimeNames();
        clean_failures=clean_failures && value && after.flags==before.flags && after.flag_capacity==before.flag_capacity && after.Total()==before.Total();++failure_prefixes;check();
      }
      require(IsEnvironmentFlagEnabled(inputs.refused_flag.c_str()),"exact successful flag insertion retry");check();
      for(BlockId id:{1,2,3,4,5,6,7,1}){
        const auto before=InspectCallingThreadRuntimeNames();const auto *actual=inputs.first->GetTypeNameById(id);
        exact=exact && actual && observation::State.live<=BoundMergeNameAssignment(before,actual?actual->size():0,true,true,true).requested;check();
      }
      const auto before=InspectCallingThreadRuntimeNames();const auto attempts=observation::State.attempts;bool merge_threw=false;
      observation::State.fail_after=attempts;try{(void)inputs.first->GetTypeNameById(8);}catch(const std::bad_alloc &){merge_threw=true;}
      observation::State.fail_after=SIZE_MAX;clean_failures=clean_failures && merge_threw && InspectCallingThreadRuntimeNames().Total()==before.Total();++failure_prefixes;check();
      require(inputs.first->GetTypeNameById(8)!=nullptr,"merge copy exact retry");check();
      const auto before_query=observation::State.attempts;observation::State.fail_after=before_query;
      const auto snapshot=InspectCallingThreadRuntimeNames();query_free=snapshot.supported && IsEnvironmentFlagEnabled(inputs.refused_flag.c_str()) && before_query==observation::State.attempts;
      observation::State.fail_after=SIZE_MAX;void *extra=::operator new(73);unmodeled=InspectCallingThreadRuntimeNames().Total()!=observation::State.live;::operator delete(extra);check();
      retained=observation::State.live;peak=observation::State.peak;
    }catch(...){error=std::current_exception();}});owner.join();if(error)std::rethrow_exception(error);
    require(exact && query_free && clean_failures && unmodeled && !observation::State.live && !observation::State.blocks,"ordinary source equality/refusal/thread retirement");
    auto report=RunRuntimeNameStorageProbe();report["ordinary_steps"]=steps;report["ordinary_exact"]=exact;report["ordinary_query_allocate0"]=query_free;
    report["refusal_prefixes"]=failure_prefixes;report["refusal_preserves_state"]=clean_failures;report["thread_exit_live"]=observation::State.live;
    report["observed_retained"]=retained;report["observed_peak"]=peak;report["unmodeled_request_control"]=unmodeled;
    std::ofstream out(argc>1?argv[1]:"runtime-name-storage.json");out<<report.dump(2)<<'\n';require(bool(out),"report write");std::cout<<report.dump()<<std::endl;return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
}
