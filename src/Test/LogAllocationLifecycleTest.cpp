#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef GLOG_NO_ABBREVIATED_SEVERITIES
#define GLOG_NO_ABBREVIATED_SEVERITIES
#endif
#include "App/Platform/Log.h"
#include <glog/logging.h>
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <new>
#include <thread>

namespace log_alloc_probe
{
struct Counts {
  std::atomic<size_t> attempts{},refusals{},live{},blocks{},peak{},maximum{};
  size_t fail_after=SIZE_MAX;
};
std::array<Counts,128> Records;
size_t Used{};
thread_local Counts *Active{};
struct alignas(std::max_align_t) Header {size_t bytes;Counts *owner;};
void Max(std::atomic<size_t> &v,size_t n){auto old=v.load();while(old<n && !v.compare_exchange_weak(old,n)){}}
void *Allocate(size_t n){
  if(n>SIZE_MAX-sizeof(Header))throw std::bad_alloc{};auto *c=Active;
  if(c){Max(c->maximum,n);if(c->attempts.fetch_add(1)>=c->fail_after){++c->refusals;throw std::bad_alloc{};}}
  auto *raw=static_cast<unsigned char*>(std::malloc(sizeof(Header)+std::max(n,size_t{1})));if(!raw)throw std::bad_alloc{};
  ::new(raw) Header{n,c};if(c){++c->blocks;Max(c->peak,c->live.fetch_add(n)+n);}return raw+sizeof(Header);
}
void Free(void *p)noexcept{if(!p)return;auto *raw=static_cast<unsigned char*>(p)-sizeof(Header);auto h=*reinterpret_cast<Header*>(raw);std::free(raw);if(h.owner){h.owner->live-=h.bytes;--h.owner->blocks;}}
}
void *operator new(size_t n){return log_alloc_probe::Allocate(n);}
void *operator new[](size_t n){return log_alloc_probe::Allocate(n);}
void operator delete(void *p)noexcept{log_alloc_probe::Free(p);}
void operator delete[](void *p)noexcept{log_alloc_probe::Free(p);}
void operator delete(void *p,size_t)noexcept{log_alloc_probe::Free(p);}
void operator delete[](void *p,size_t)noexcept{log_alloc_probe::Free(p);}
void *operator new(size_t n,const std::nothrow_t &)noexcept{try{return log_alloc_probe::Allocate(n);}catch(...){return nullptr;}}
void *operator new[](size_t n,const std::nothrow_t &)noexcept{try{return log_alloc_probe::Allocate(n);}catch(...){return nullptr;}}
void operator delete(void *p,const std::nothrow_t &)noexcept{log_alloc_probe::Free(p);}
void operator delete[](void *p,const std::nothrow_t &)noexcept{log_alloc_probe::Free(p);}

namespace
{
using namespace cutum;using Json=nlohmann::json;using namespace log_alloc_probe;
const std::string Message="token=log_lifecycle_complete";
void Require(bool ok,const char *message){if(!ok){std::cerr<<"log_allocation_lifecycle: "<<message<<'\n';std::exit(1);}}
struct Sink final:google::LogSink {
  std::atomic<bool> fail{},recovering{};
  std::atomic<size_t> recovered{},outer{};
  void send(google::LogSeverity,const char *,const char *,int,const google::LogMessageTime &,const char *msg,size_t n)override {
    if(fail.exchange(false))throw std::bad_alloc{};
    const auto contains=[&](const char *word){const size_t size=std::strlen(word);for(size_t i=0;i+size<=n;++i)if(!std::memcmp(msg+i,word,size))return true;return false;};
    if(recovering.load() && contains("token=log_lifecycle_complete"))++recovered;
    if(contains("outer_sentinel/end"))++outer;
  }
} Capture;
std::atomic<bool> PrefixFail{};
void Prefix(std::ostream &out,const google::LogMessage &,void *){if(PrefixFail.exchange(false))throw std::bad_alloc{};out.write("test_prefix",11);}
void Emit(bool error){if(error)CubatariumLogError("lifecycle",Message);else CubatariumLogInfo("lifecycle",Message);}
Json Snapshot(const Counts &c){return {{"attempts",c.attempts.load()},{"refusals",c.refusals.load()},{"live",c.live.load()},{"blocks",c.blocks.load()},{"peak",c.peak.load()},{"max_request",c.maximum.load()}};}
Json Run(bool error,bool heap,size_t failure,int callback=0,bool retry=true)
{
  Require(Used<Records.size(),"record limit");auto &c=Records[Used++];c.fail_after=failure;
  bool threw=false,retry_threw=false;size_t after_failure{},blocks_after_failure{};
  const auto recovered_before=Capture.recovered.load(),outer_before=Capture.outer.load();
  std::thread worker([&]{
    std::unique_ptr<google::LogMessage> outer;
    if(heap){outer=std::make_unique<google::LogMessage>(__FILE__,__LINE__);outer->stream()<<"outer_sentinel";}
    Active=&c;Capture.recovering=false;
    if(callback==1)PrefixFail=true;
    if(callback==2)Capture.fail=true;
    try{Emit(error);}catch(const std::bad_alloc &){threw=true;}
    after_failure=c.live.load();blocks_after_failure=c.blocks.load();
    if(retry){c.fail_after=SIZE_MAX;Capture.recovering=true;try{Emit(error);}catch(const std::bad_alloc &){retry_threw=true;}}
    Active=nullptr;
    if(outer)outer->stream()<<"/end";
  });worker.join();
  auto result=Snapshot(c);result["error"]=error;result["heap_path"]=heap;result["failure_prefix"]=failure;result["callback"]=callback;
  result["threw"]=threw;result["retry_threw"]=retry_threw;result["after_failure"]=after_failure;result["blocks_after_failure"]=blocks_after_failure;
  result["retry_delivered"]=Capture.recovered.load()>recovered_before;result["outer_preserved"]=!heap || Capture.outer.load()==outer_before+1;
  result["pass"]=(!retry || (!retry_threw && result["retry_delivered"].get<bool>())) && c.live==0 && c.blocks==0 && result["outer_preserved"].get<bool>();
  if(retry && !heap)result["pass"]=result["pass"].get<bool>() && c.maximum.load()<4096;
  return result;
}
}
int main(int argc,char **argv)
{
  const bool baseline=argc>1 && std::string(argv[1])=="--baseline";
  const auto output=std::filesystem::path(argc>(baseline?2:1)?argv[baseline?2:1]:"log-allocation-lifecycle.json");
  const auto folder=std::filesystem::absolute(output.parent_path()/"log-allocation-lifecycle-fixture");std::filesystem::create_directories(folder);
  _putenv_s("CUBATARIUM_LOG_DIR",folder.string().c_str());CubatariumSetSuppressErrorDialogs(true);CubatariumInitLogging("log_allocation_lifecycle",false);
  Emit(false);Emit(true);google::AddLogSink(&Capture);
  Json report={{"schema","log-allocation-lifecycle.v1"},{"cases",Json::array()}};
  if(baseline){auto result=Run(false,false,2);report["baseline"]=result;
    const bool reproduced=result["threw"].get<bool>() && result["live"]==32 && result["blocks"]==2 && result["retry_delivered"].get<bool>();
    google::RemoveLogSink(&Capture);CubatariumShutdownLogging();report["status"]=reproduced?"REPRODUCED":"FAIL";
    std::ofstream(output)<<report.dump(2)<<'\n';std::cout<<report.dump()<<'\n';return reproduced?0:1;
  }
  size_t failed=0,prefixes=0;
  for(bool error:{false,true})for(bool heap:{false,true}){
    const auto normal=Run(error,heap,SIZE_MAX,0,false);Require(normal["pass"].get<bool>(),"normal log retires");const auto attempts=normal["attempts"].get<size_t>();
    report["cases"].push_back(normal);
    for(size_t i=0;i<attempts;++i){auto result=Run(error,heap,i);if(!result["pass"].get<bool>() || result["refusals"]==0)++failed;report["cases"].push_back(result);++prefixes;}
  }
  google::InstallPrefixFormatter(Prefix,nullptr);
  for(bool heap:{false,true}){auto result=Run(false,heap,SIZE_MAX,1);if(!result["pass"].get<bool>() || !result["threw"].get<bool>())++failed;report["cases"].push_back(result);}
  google::InstallPrefixFormatter(nullptr,nullptr);
  for(bool heap:{false,true}){auto result=Run(false,heap,SIZE_MAX,2);if(!result["pass"].get<bool>() || !result["threw"].get<bool>())++failed;report["cases"].push_back(result);}
  std::array<std::thread,6> peers;std::array<Counts*,6> counters{};
  for(size_t i=0;i<peers.size();++i){Require(Used<Records.size(),"parallel record limit");counters[i]=&Records[Used++];peers[i]=std::thread([&,i]{Active=counters[i];for(int n=0;n<8;++n)Emit(false);Active=nullptr;});}
  for(auto &peer:peers)peer.join();bool parallel=true;for(const auto *c:counters)parallel=parallel && c->live==0 && c->blocks==0;if(!parallel)++failed;
  google::RemoveLogSink(&Capture);CubatariumShutdownLogging();size_t final_bytes{},final_blocks{};for(size_t i=0;i<Used;++i){final_bytes+=Records[i].live;final_blocks+=Records[i].blocks;}
  if(final_bytes || final_blocks)++failed;
  report["ordinary_refusal_prefixes"]=prefixes;report["parallel_workers"]=6;report["parallel_messages"]=48;report["parallel_retired"]=parallel;
  report["final_bytes"]=final_bytes;report["final_blocks"]=final_blocks;report["failed_cases"]=failed;report["status"]=failed?"FAIL":"PASS";
  report["scope"]="Actual warmed Cubatarium INFO/ERROR and glog TLS/heap data lifecycle under persistent ordinary-new refusal,throwing prefix/sink,same-thread retry and serialized concurrent calls. No initialization,CRT heap,stack,GPU or process-total verdict.";
  std::ofstream out(output);out<<report.dump(2)<<'\n';Require(bool(out),"report write");std::cout<<report.dump()<<'\n';return failed?1:0;
}
