#pragma once
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/AllocatorSourcePacket.h"
#include "Core/Memory/NameStorageSource.h"
namespace cutum {
class UApplication;class UNewWorldScreen;class ULoadWorldScreen;
enum class ApplicationActionKind:size_t {Empty,ReturnMenu,ShowMenu,RetrySave,ShutdownComplete,CreateWorld,LoadWorld,Unknown};
struct ApplicationActionLayout {std::array<size_t,6> captures{},implementations{};size_t function_inline{},runner_inline{},request_inline{};bool supported{};};
ApplicationActionLayout GetApplicationActionLayout() noexcept;
struct ApplicationActionStorage {
  CallbackStorageSnapshot callback;ApplicationActionKind kind{ApplicationActionKind::Unknown};
  std::array<size_t,4> captured{}; // settings text,pack vectors,pack text,world name
  uintptr_t target{},borrowed_host{};bool overflow{};
  size_t Captured() const noexcept {NameStorageEnvelope e;for(auto n:captured)NameStorageAdd(e,n);return e.requested;}
  size_t Total() const noexcept {NameStorageEnvelope e;NameStorageAdd(e,callback.requests);NameStorageAdd(e,Captured());return e.requested;}
  bool Supported() const noexcept {return callback.supported && !overflow;}
};
ApplicationActionStorage InspectApplicationAction(const std::function<void()> &fn,const UApplication *application,const UNewWorldScreen *create_screen,const ULoadWorldScreen *load_screen,bool history_known) noexcept;
struct WorldOperationStorage {
  uintptr_t object{},core{},world{};std::array<size_t,6> requests{};
  bool supported{},present{},active{},overflow{};size_t stage{};
  size_t Total()const noexcept {NameStorageEnvelope e;for(auto n:requests)NameStorageAdd(e,n);return e.requested;}
};
struct ApplicationOperationStorage {
  uintptr_t application{};std::array<ApplicationActionStorage,2> actions;WorldOperationStorage runner;
  uint64_t epoch{};std::thread::id thread;bool supported{},overflow{};
  size_t Wrappers()const noexcept {NameStorageEnvelope e;for(const auto &a:actions)NameStorageAdd(e,a.callback.requests);return e.requested;}
  size_t Additional()const noexcept {NameStorageEnvelope e;NameStorageAdd(e,runner.Total());for(const auto &a:actions)NameStorageAdd(e,a.Captured());return e.requested;}
  size_t Total()const noexcept {NameStorageEnvelope e;NameStorageAdd(e,Wrappers());NameStorageAdd(e,Additional());return e.requested;}
};
struct ApplicationOperationEnvelope {size_t applications{},requested{},wrappers{},additional{};bool supported{},overflow{};};
inline ApplicationOperationEnvelope JoinApplicationOperations(const AllocatorSourcePhase &phase,const std::vector<const ApplicationOperationStorage*> &views,size_t expected_applications,bool complete_domain,bool parent_coverage) noexcept {
  ApplicationOperationEnvelope out;out.supported=phase.IsOpen() && phase.epoch && phase.participants==1 && complete_domain && parent_coverage;NameStorageEnvelope total;total.supported=out.supported;
  for(size_t i=0;i<views.size();++i){const auto *s=views[i];if(!s || !s->supported || s->overflow || !s->application || s->epoch!=phase.epoch || s->thread!=std::this_thread::get_id()){out.supported=false;continue;}
    bool alias=false;for(size_t j=0;j<i;++j)if(const auto *p=views[j])if(p->application==s->application){alias=true;
      if(p->runner.object!=s->runner.object || p->runner.requests!=s->runner.requests || p->runner.stage!=s->runner.stage || p->runner.active!=s->runner.active)out.supported=false;
      for(size_t k=0;k<s->actions.size();++k){const auto &a=s->actions[k],&b=p->actions[k];if(a.callback.cell!=b.callback.cell || a.callback.owner!=b.callback.owner || a.target!=b.target || a.kind!=b.kind || a.callback.requests!=b.callback.requests || a.captured!=b.captured || a.callback.present!=b.callback.present)out.supported=false;}}
    if(alias)continue;++out.applications;NameStorageEnvelope wrappers,additional;wrappers.requested=out.wrappers;additional.requested=out.additional;
    for(auto n:s->runner.requests){NameStorageAdd(total,n);NameStorageAdd(additional,n);}
    for(const auto &a:s->actions){NameStorageAdd(total,a.callback.requests);NameStorageAdd(wrappers,a.callback.requests);for(auto n:a.captured){NameStorageAdd(total,n);NameStorageAdd(additional,n);}}
    out.wrappers=wrappers.requested;out.additional=additional.requested;out.overflow=out.overflow || wrappers.overflow || additional.overflow;
  }
  out.requested=total.requested;out.overflow=out.overflow || total.overflow;if(out.overflow || out.applications!=expected_applications || !phase.IsOpen())out.supported=false;return out;
}
} // namespace cutum
