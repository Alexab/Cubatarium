#pragma once
#include "WorldGen/Core/RuntimeNameStorage.h"
#include <nlohmann/json.hpp>
#include <array>
#include <thread>
#include <stdexcept>

namespace cutum {
struct RuntimeNameStorageProbe {
  struct Inputs {
    std::vector<std::string> flags;std::string refused_flag;
    std::unique_ptr<UBlockMergeRegistry> first,second;
    Inputs():refused_flag("CUBA_QA_W11_TRUE_"+std::string(6000,'t')),first(std::make_unique<UBlockMergeRegistry>()),second(std::make_unique<UBlockMergeRegistry>()){
      for(size_t i=0;i<256;++i){
        const std::array<size_t,8> sizes={12,15,16,31,4079,4080,4096,8192};
        std::string name="QAW11_"+std::to_string(i)+"_";name.resize(sizes[i%sizes.size()],char('a'+i%26));flags.push_back(std::move(name));
      }
      const std::array<size_t,8> lengths={7,16,4079,4080,8192,32,12,16000};
      for(size_t i=0;i<lengths.size();++i)first->IdToName.emplace(BlockId(i+1),std::string(lengths[i],char('a'+i)));
      second->IdToName.emplace(1,"second_registry");
    }
  };
  struct ScopedFlag {
    std::string name,previous;bool existed{};
    explicit ScopedFlag(const std::string &key):name(key){if(const auto *old=std::getenv(name.c_str())){previous=old;existed=true;}Set("1");}
    void Set(const char *value){
#if defined(_WIN32)
      if(_putenv_s(name.c_str(),value)!=0)throw std::runtime_error("test flag update");
#else
      if(value && *value)setenv(name.c_str(),value,1);else unsetenv(name.c_str());
#endif
    }
    ~ScopedFlag(){
#if defined(_WIN32)
      (void)_putenv_s(name.c_str(),existed?previous.c_str():"");
#else
      if(existed)setenv(name.c_str(),previous.c_str(),1);else unsetenv(name.c_str());
#endif
    }
  };
  static nlohmann::json Run(){
    using Json=nlohmann::json;Inputs inputs;ScopedFlag truth(inputs.refused_flag);Json result;std::exception_ptr error;
    std::thread owner([&]{try{
      const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
      const auto fresh=InspectCallingThreadRuntimeNames();require(fresh.supported && !fresh.Total() && !fresh.flags,"fresh name cache has no backing");
      Json flags=Json::array(),names=Json::array();
      for(size_t i=0;i<140;++i){const auto before=InspectCallingThreadRuntimeNames();const auto upper=BoundEnvironmentFlagInsertion(before,inputs.flags[i].size(),true,true);
        const bool expected=IsEnvironmentFlagEnabledUncached(inputs.flags[i].c_str());
        require(IsEnvironmentFlagEnabled(inputs.flags[i].c_str())==expected,"cached value matches existing parser");
        const auto after=InspectCallingThreadRuntimeNames();require(after.supported && after.flags==before.flags+1 && after.Total()<=upper.requested,"name/vector growth source upper");
        if(i==0 || i==6 || i==139)flags.push_back({{"entry",i},{"capacity",after.flag_capacity},{"vector_requests",after.environment_vector},{"name_requests",after.environment_names}});
      }
      const auto true_before=InspectCallingThreadRuntimeNames();require(IsEnvironmentFlagEnabled(inputs.refused_flag.c_str()),"true flag cached");
      truth.Set("0");require(!IsEnvironmentFlagEnabledUncached(inputs.refused_flag.c_str()) && IsEnvironmentFlagEnabled(inputs.refused_flag.c_str()),"process-constant cached value preserved");
      for(BlockId id:{1,2,3,4,5,6,7,8,1}){
        const auto before=InspectCallingThreadRuntimeNames();const auto &expected=inputs.first->IdToName.at(id);
        const auto upper=BoundMergeNameAssignment(before,expected.size(),true,true,true);
        const auto *actual=inputs.first->GetTypeNameById(id);const auto after=InspectCallingThreadRuntimeNames();
        require(actual && *actual==expected && after.Total()<=upper.requested,"actual merge-name copy/growth retains exact value");
        names.push_back({{"id",id},{"length",expected.size()},{"retained_capacity",after.merge_capacity},{"requests",after.merge_name}});
      }
      const auto before_missing=InspectCallingThreadRuntimeNames();require(!inputs.first->GetTypeNameById(65530),"missing name lookup");
      require(InspectCallingThreadRuntimeNames().Total()==before_missing.Total(),"missing lookup preserves previous capacity");
      require(*inputs.second->GetTypeNameById(1)=="second_registry","one per-thread name source shared across registries");
      const auto *copied=inputs.first->GetTypeNameById(5);inputs.first.reset();require(copied && copied->size()==8192 && (*copied)[0]=='e',"copied name survives source registry retirement");
      const auto final=InspectCallingThreadRuntimeNames();
      require(!BoundEnvironmentFlagInsertion(final,20,false,true).supported && !BoundEnvironmentFlagInsertion(final,20,true,false).supported &&
        !BoundMergeNameAssignment(final,20,false,true,true).supported && !BoundMergeNameAssignment(final,20,true,false,true).supported,"unknown history/source controls");
      auto huge=final;huge.environment_names=SIZE_MAX;require(BoundEnvironmentFlagInsertion(huge,200,true,true).overflow,"checked source addition overflow");
      result={{"schema","runtime-name-storage.v1"},{"status","PASS"},{"entry_bytes",final.layout.flag_entry},
        {"inline_bytes",{final.layout.environment_inline,final.layout.merge_inline}},{"large_threshold",final.layout.large_threshold},{"large_slack",final.layout.large_slack},
        {"flag_growth",flags},{"merge_growth",names},{"final_flags",final.flags},{"final_capacity",final.flag_capacity},
        {"final_vector_requests",final.environment_vector},{"final_name_requests",final.environment_names},{"final_merge_requests",final.merge_name},
        {"total_requests",final.Total()},{"fresh_no_backing",true},{"cached_value_constant",true},{"source_registry_retired",true},{"history_overflow_controls",true},
        {"scope","Calling-thread Environment name/vector and merge-name copied storage;W03 inline shells excluded. Default MSVC requested bytes and explicit input/capacity history;not all OS threads/CRT/environment-block/process acceptance."}};
      (void)true_before;
    }catch(...){error=std::current_exception();}});owner.join();if(error)std::rethrow_exception(error);return result;
  }
};
inline nlohmann::json RunRuntimeNameStorageProbe(){return RuntimeNameStorageProbe::Run();}
}
