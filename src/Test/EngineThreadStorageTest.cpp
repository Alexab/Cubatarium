#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/OrdinaryNewObservation.h"
#include "Test/EngineThreadStorageFixture.h"
#include <fstream>
#include <iostream>

int main(int argc,char **argv){
  using namespace cutum;const std::string text(8192,'q');
  const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
  try{
    size_t equality{},small_attempts{},refusals{};bool query_free{},negative{};std::exception_ptr error;
    std::thread owner([&]{try{
      observation::Scope observed;const auto before=InspectCallingThreadEngineStorage(true,true);
      const auto upper=BoundEngineThreadTransition(before,EngineThreadFixtureUpper());
      const auto exercise=ExerciseEngineThreadStorage(text,[&](const auto &s){
        if(s.Total()!=observation::State.live){std::cerr<<"source="<<s.Total()<<" actual="<<observation::State.live<<" at="<<equality<<'\n';throw std::runtime_error("engine ordinary request equality");}++equality;
      });
      require(observation::State.peak<=upper.requested,"declared engine transition source upper");
      const auto attempts=observation::State.attempts;observation::State.fail_after=attempts;
      query_free=InspectCallingThreadEngineStorage(true,true).requests==exercise.final.requests && observation::State.attempts==attempts;
      observation::State.fail_after=SIZE_MAX;void *unmodeled=::operator new(73);negative=InspectCallingThreadEngineStorage(true,true).Total()!=observation::State.live;::operator delete(unmodeled);
    }catch(...){error=std::current_exception();}});owner.join();if(error)std::rethrow_exception(error);
    require(query_free && negative && observation::State.live==0 && observation::State.blocks==0,"engine queries and thread-exit requests");
    std::thread baseline([&]{try{observation::Scope observed;PopulateSmallEngineThreadStorage(text);small_attempts=observation::State.attempts;
      require(InspectCallingThreadEngineStorage(true,true).Total()==observation::State.live,"small source equality");
    }catch(...){error=std::current_exception();}});baseline.join();if(error)std::rethrow_exception(error);require(!observation::State.live && small_attempts,"small source retirement");
    for(size_t prefix=0;prefix<small_attempts;++prefix){
      std::thread refusal([&]{try{observation::Scope observed(prefix);bool caught=false;
        try{PopulateSmallEngineThreadStorage(text);}catch(const std::bad_alloc&){caught=true;}
        require(caught,"controlled source request refused");observation::State.fail_after=SIZE_MAX;
        require(InspectCallingThreadEngineStorage(true,true).Total()==observation::State.live,"refused engine source retains exact backing");
        PopulateSmallEngineThreadStorage(text);require(InspectCallingThreadEngineStorage(true,true).Total()==observation::State.live,"same-input engine retry");++refusals;
      }catch(...){error=std::current_exception();}});refusal.join();if(error)std::rethrow_exception(error);require(!observation::State.live && !observation::State.blocks,"refused engine thread retirement");
    }
    auto report=RunEngineThreadStorageProbe();report["ordinary_equality_checks"]=equality;report["ordinary_query_allocate0"]=query_free;
    report["ordinary_refusal_prefixes"]=refusals;report["thread_exit_live"]=observation::State.live;report["unmodeled_request_control"]=negative;
    std::ofstream out(argc>1?argv[1]:"engine-thread-storage.json");out<<report.dump(2)<<'\n';require(bool(out),"engine report write");std::cout<<report.dump()<<'\n';return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
