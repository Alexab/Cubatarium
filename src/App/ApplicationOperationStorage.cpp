#include "App/ApplicationOperationStorage.h"
#include "App/StoredApplicationActions.h"
#include "App/Application.h"
#include "App/Core.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Screens/LoadWorldScreen.h"
#include "Core/Memory/RootStorageSource.h"
namespace cutum {
namespace {
void Text(NameStorageEnvelope &e,const std::string &s) noexcept {const auto n=NameStorageExternalBytes(s);if(n)NameStorageAdd(e,NameStorageBufferRequest(n,1,e));}
size_t Settings(const ProceduralSettings &s,NameStorageEnvelope &e) noexcept {NameStorageEnvelope local;local.supported=true;for(const auto *t:{&s.SeedText,&s.WorldGenPackId,&s.WorldGenPresetId})Text(local,*t);e.overflow=e.overflow || local.overflow;e.supported=e.supported && local.supported;return local.requested;}
void Packs(const ResourcePackSelection &s,NameStorageEnvelope &vectors,NameStorageEnvelope &text) noexcept {
  for(const auto *v:{&s.Primary,&s.Secondary}){NameStorageAdd(vectors,NameStorageBufferRequest(v->capacity(),sizeof(std::string),vectors));for(const auto &id:*v)Text(text,id);}Text(text,s.WorldgenOwner);
}
template<class Capture> bool CaptureAction(ApplicationActionStorage &out,const std::function<void()> &fn,const void *expected,ApplicationActionKind kind) noexcept {
  const auto *target=fn.target<Capture>();if(!target)return false;out.kind=kind;
  out.callback.capture_inline=sizeof(Capture);out.target=reinterpret_cast<uintptr_t>(target);
  if(!expected || target->owner!=expected){out.callback.supported=false;return true;}
  out.callback.owner=reinterpret_cast<uintptr_t>(target->owner);out.callback.requests=KnownCallbackRequestBytes<Capture>(fn);
  const auto address=reinterpret_cast<uintptr_t>(target),cell=reinterpret_cast<uintptr_t>(&fn);if((address>=cell && address-cell<sizeof(fn))!=(out.callback.requests==0))out.callback.supported=false;
  return true;
}
}
ApplicationActionLayout GetApplicationActionLayout() noexcept {
  ApplicationActionLayout out;const std::function<void()> empty;
  out.captures={sizeof(ApplicationReturnMenuAction),sizeof(ApplicationShowMenuAction),sizeof(ApplicationRetrySaveAction),sizeof(ApplicationShutdownCompleteAction),sizeof(CreateWorldMenuAction),sizeof(LoadWorldMenuAction)};
  out.implementations={KnownCallbackRequestBytes<ApplicationReturnMenuAction>(empty),KnownCallbackRequestBytes<ApplicationShowMenuAction>(empty),KnownCallbackRequestBytes<ApplicationRetrySaveAction>(empty),KnownCallbackRequestBytes<ApplicationShutdownCompleteAction>(empty),KnownCallbackRequestBytes<CreateWorldMenuAction>(empty),KnownCallbackRequestBytes<LoadWorldMenuAction>(empty)};
  out.function_inline=sizeof(empty);out.runner_inline=sizeof(UWorldOperationRunner);out.request_inline=sizeof(WorldRunnerRequest);out.supported=RootOwnerRepresentationSupported;return out;
}
ApplicationActionStorage InspectApplicationAction(const std::function<void()> &fn,const UApplication *application,const UNewWorldScreen *create_screen,const ULoadWorldScreen *load_screen,bool known) noexcept {
  ApplicationActionStorage out;out.callback.cell=reinterpret_cast<uintptr_t>(&fn);out.callback.present=bool(fn);out.callback.supported=known && RootOwnerRepresentationSupported;
  if(!fn){out.kind=ApplicationActionKind::Empty;return out;}if(!out.callback.supported)return out;
  if(CaptureAction<ApplicationReturnMenuAction>(out,fn,application,ApplicationActionKind::ReturnMenu) || CaptureAction<ApplicationShowMenuAction>(out,fn,application,ApplicationActionKind::ShowMenu) ||
    CaptureAction<ApplicationRetrySaveAction>(out,fn,application,ApplicationActionKind::RetrySave) || CaptureAction<ApplicationShutdownCompleteAction>(out,fn,application,ApplicationActionKind::ShutdownComplete))return out;
  NameStorageEnvelope e;e.supported=true;
  if(CaptureAction<CreateWorldMenuAction>(out,fn,create_screen,ApplicationActionKind::CreateWorld)){
    if(!out.callback.supported)return out;
    if(!create_screen->HasActionHostForStorage(application)){out.callback.supported=false;return out;}
    const auto &capture=*fn.target<CreateWorldMenuAction>();out.borrowed_host=reinterpret_cast<uintptr_t>(application);
    out.captured[0]=Settings(capture.settings,e);NameStorageEnvelope v,t;v.supported=t.supported=true;Packs(capture.packs,v,t);out.captured[1]=v.requested;out.captured[2]=t.requested;out.overflow=e.overflow || v.overflow || t.overflow;
  }else if(CaptureAction<LoadWorldMenuAction>(out,fn,load_screen,ApplicationActionKind::LoadWorld)){
    if(!out.callback.supported)return out;
    if(!load_screen->HasActionHostForStorage(application)){out.callback.supported=false;return out;}
    Text(e,fn.target<LoadWorldMenuAction>()->worldName);out.captured[3]=e.requested;out.borrowed_host=reinterpret_cast<uintptr_t>(application);out.overflow=e.overflow;
  }else out.callback.supported=false;
  NameStorageEnvelope sum;sum.supported=true;NameStorageAdd(sum,out.callback.requests);for(auto n:out.captured)NameStorageAdd(sum,n);out.overflow=out.overflow || sum.overflow;if(out.overflow)out.callback.supported=false;return out;
}
WorldOperationStorage UWorldOperationRunner::InspectOwnedStorage(const UCore *core,const UWorld *world,bool history_known)const noexcept {
  WorldOperationStorage out;out.object=reinterpret_cast<uintptr_t>(this);out.core=reinterpret_cast<uintptr_t>(&Core);out.world=reinterpret_cast<uintptr_t>(&World);out.present=true;out.active=Active;out.stage=static_cast<size_t>(CurrentStage);
  out.supported=RootOwnerRepresentationSupported && history_known && core==&Core && world==&World;if(!out.supported)return out;
  out.requests[0]=sizeof(*this);NameStorageEnvelope e;e.supported=true;Text(e,Request.worldName);out.requests[1]=e.requested;out.requests[2]=Settings(Request.settings,e);
  NameStorageEnvelope vectors,text;vectors.supported=text.supported=true;Packs(Request.packs,vectors,text);out.requests[3]=vectors.requested;out.requests[4]=text.requested;
  NameStorageEnvelope retained;retained.supported=true;Text(retained,Error);Text(retained,PendingWorldName);out.requests[5]=retained.requested;
  NameStorageEnvelope total;total.supported=true;for(auto n:out.requests)NameStorageAdd(total,n);out.overflow=e.overflow || vectors.overflow || text.overflow || retained.overflow || total.overflow;if(out.overflow)out.supported=false;return out;
}
ApplicationOperationStorage UApplication::InspectOperationStorage(const AllocatorSourcePhase &phase,bool known)const noexcept {
  ApplicationOperationStorage out;out.application=reinterpret_cast<uintptr_t>(this);out.epoch=phase.epoch;out.thread=std::this_thread::get_id();known=known && phase.IsOpen() && phase.epoch && phase.participants==1;
  const auto *screen=known && GuiContext?GuiContext->GetScreen():nullptr;
  const auto *create=dynamic_cast<const UNewWorldScreen*>(screen);const auto *load=dynamic_cast<const ULoadWorldScreen*>(screen);
  out.actions[0]=InspectApplicationAction(PendingMenuAction,this,create,load,known);out.actions[1]=InspectApplicationAction(WorldOpOnComplete,this,create,load,known);
  out.runner.supported=known;if(WorldOpRunner)out.runner=WorldOpRunner->InspectOwnedStorage(Core.get(),World.get(),known);
  NameStorageEnvelope total;total.supported=true;NameStorageAdd(total,out.runner.Total());for(const auto &a:out.actions)NameStorageAdd(total,a.Total());
  out.overflow=total.overflow || out.runner.overflow;out.supported=known && !out.overflow && out.runner.supported;for(const auto &a:out.actions)out.supported=out.supported && a.Supported();return out;
}
MemberCallbackStorageSnapshot UApplication::InspectStoredActionCallbacks(bool known)const noexcept {
  MemberCallbackStorageSnapshot out;const auto *screen=known && GuiContext?GuiContext->GetScreen():nullptr;const auto *create=dynamic_cast<const UNewWorldScreen*>(screen);const auto *load=dynamic_cast<const ULoadWorldScreen*>(screen);
  out.Set(MemberCallbackCell::PendingAction,InspectApplicationAction(PendingMenuAction,this,create,load,known).callback);
  out.Set(MemberCallbackCell::OperationComplete,InspectApplicationAction(WorldOpOnComplete,this,create,load,known).callback);
#if defined(_MSVC_STL_VERSION)
  if(PendingMenuAction && !out.rows[33].supported)out.unknown_types[33]=PendingMenuAction.target_type().raw_name();
  if(WorldOpOnComplete && !out.rows[34].supported)out.unknown_types[34]=WorldOpOnComplete.target_type().raw_name();
#endif
  return out;
}
} // namespace cutum
