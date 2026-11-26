#pragma once
#include "App/Application.h"
#include "App/Core.h"
#include "App/StoredApplicationActions.h"
#include "Core/Memory/RootStorageSource.h"
#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Screens/LoadWorldScreen.h"
#include <nlohmann/json.hpp>
namespace cutum {
struct ApplicationOperationSourceProbe {
  static void Fail(UWorldOperationRunner &runner,const std::string &error){UNullProgressSink sink;runner.Fail(error,sink);}
  static void PendingName(UWorldOperationRunner &runner,const std::string &name){runner.PendingWorldName=name;}
  static void SetPending(UApplication &app,std::function<void()> callback){app.ScheduleDeferredMenuAction(std::move(callback));}
  static void ExecutePending(UApplication &app){app.RunPendingMenuAction();}
  static void Clear(UApplication &app){app.PendingMenuAction={};app.WorldOpOnComplete={};app.WorldOpRunner.reset();}
  static void ShowProgress(UApplication &app) { app.ShowWorldProgressScreen(); }
  static const UWorldProgressScreen *Progress(const UApplication &app) { return app.ProgressScreen; }
  static UDeathScreen &BuildDeath(UApplication &app)
  {
    app.BuildDeathOverlay();
    return *app.DeathScreen;
  }
  static void UpdateDeath(UApplication &app) { app.UpdateDeathOverlay(); }
  static void SetGameState(UApplication &app) { app.State = AppState::InGame; }
  static bool IsInGame(const UApplication &app) { return app.State == AppState::InGame; }
  static void RetireDeath(UApplication &app) { app.DeathScreen.reset(); }
  static void DetachProgress(UApplication &app)
  {
    app.ProgressScreen = nullptr;
    app.GetGui().SetScreen(nullptr);
  }
  static void SetMenuSession(UApplication &app, bool resume)
  {
    app.State = AppState::MainMenu;
    app.WorldSessionActive = resume;
  }
  static void ShowMain(UApplication &app) { app.ShowMainMenu(); }
  static void DetachMain(UApplication &app)
  {
    app.MainMenuScreen = nullptr;
    app.GetGui().SetScreen(nullptr);
  }
  static std::function<void()> TakePending(UApplication &app){return std::move(app.PendingMenuAction);}
};
inline WorldRunnerRequest ApplicationOperationFixtureRequest(int variant){
  WorldRunnerRequest request;request.op=WorldRunnerOp::Create;
  if(variant){
    request.worldName=std::string(4095,'w');request.settings.SeedText=std::string(4095,'s');request.settings.WorldGenPackId=std::string(6000,'p');request.settings.WorldGenPresetId=std::string(193,'r');
    const size_t count=variant==2?180:3;request.packs.Primary.assign(count,std::string(97,'a'));request.packs.Secondary.assign(2,std::string(4095,'b'));request.packs.WorldgenOwner=std::string(4095,'o');
    request.worldName.reserve(9000);request.packs.Primary.reserve(count+100);
  }
  return request;
}
inline nlohmann::json ApplicationOperationSourceJson(const ApplicationOperationStorage &s){
  nlohmann::json actions=nlohmann::json::array();for(const auto &a:s.actions)actions.push_back({{"kind",static_cast<size_t>(a.kind)},{"supported",a.Supported()},{"present",a.callback.present},{"implementation",a.callback.requests},{"captured",a.captured},{"total",a.Total()}});
  return {{"status",s.supported?"PASS":"UNSUPPORTED"},{"actions",actions},{"runner_supported",s.runner.supported},{"runner_present",s.runner.present},{"runner_active",s.runner.active},{"runner_requests",s.runner.requests},
    {"wrappers_already_in_member_storage",s.Wrappers()},{"additional",s.Additional()},{"total",s.Total()},{"whole_runtime_complete",false}};
}
inline std::function<void()> MakeApplicationActionFixture(size_t kind,UApplication &application,UNewWorldScreen &create,ULoadWorldScreen &load,const WorldRunnerRequest &input){
  switch(kind){
  case 1:return ApplicationReturnMenuAction{&application};
  case 2:return ApplicationShowMenuAction{&application};
  case 3:return ApplicationRetrySaveAction{&application};
  case 4:return ApplicationShutdownCompleteAction{&application};
  case 5:{auto value=CreateWorldMenuAction{&create,input.settings,input.packs,input.view,input.gameMode,input.difficulty};return value;}
  case 6:return LoadWorldMenuAction{&load,input.worldName};
  default:return {};
  }
}
inline nlohmann::json RunApplicationOperationStorageProbe(UApplication &application,UCore &core,UWorld &world){
  using Json=nlohmann::json;const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
  const auto layout=GetApplicationActionLayout();require(layout.supported,"application source ABI profile");
  UNewWorldScreen create(&application),foreign_create(nullptr);ULoadWorldScreen load(&application),foreign_load(nullptr);
  Json cases=Json::array();auto input=ApplicationOperationFixtureRequest(2);AllocatorSourcePhase phase(NewRootStorageEpoch(),1);
  for(size_t kind=1;kind<=6;++kind){auto fn=MakeApplicationActionFixture(kind,application,create,load,input);const auto s=InspectApplicationAction(fn,&application,&create,&load,true);
    require(s.Supported() && static_cast<size_t>(s.kind)==kind && s.callback.capture_inline==layout.captures[kind-1] && s.callback.requests==layout.implementations[kind-1],"six actual typed action source profiles");
    if(kind==5)require(!InspectApplicationAction(fn,&application,&foreign_create,&load,true).Supported(),"foreign create owner rejected before dereference");
    if(kind==6)require(!InspectApplicationAction(fn,&application,&create,&foreign_load,true).Supported(),"foreign load owner rejected before dereference");
    require(!InspectApplicationAction(fn,&application,&create,&load,false).Supported(),"unknown action history rejected");
    cases.push_back({{"kind",kind},{"capture",s.callback.capture_inline},{"implementation",s.callback.requests},{"captured",s.captured},{"total",s.Total()}});
  }
  auto runner=std::make_unique<UWorldOperationRunner>(core,world);const auto cold=runner->InspectOwnedStorage(&core,&world,true);
  runner->Start(input);const auto active=runner->InspectOwnedStorage(&core,&world,true);
  ApplicationOperationSourceProbe::PendingName(*runner,std::string(9000,'n'));ApplicationOperationSourceProbe::Fail(*runner,"Application source fixture");const auto failed=runner->InspectOwnedStorage(&core,&world,true);
  runner->Start(ApplicationOperationFixtureRequest(0));const auto cleared=runner->InspectOwnedStorage(&core,&world,true);
  require(cold.supported && active.supported && failed.supported && cleared.supported && !failed.active && cleared.requests[5]>0,"runner owned request and retained cleared buffers");
  require(!runner->InspectOwnedStorage(nullptr,&world,true).supported && !runner->InspectOwnedStorage(&core,nullptr,true).supported && !runner->InspectOwnedStorage(&core,&world,false).supported,"runner borrowed owner and factory history controls");
  auto pending=MakeApplicationActionFixture(5,application,create,load,input),completion=MakeApplicationActionFixture(4,application,create,load,input);
  ApplicationOperationStorage packet;packet.application=reinterpret_cast<uintptr_t>(&application);packet.epoch=phase.epoch;packet.thread=std::this_thread::get_id();packet.supported=true;
  packet.actions={InspectApplicationAction(pending,&application,&create,&load,true),InspectApplicationAction(completion,&application,&create,&load,true)};packet.runner=cleared;
  std::vector<const ApplicationOperationStorage*> views{&packet};const auto joined=JoinApplicationOperations(phase,views,1,true,true);
  for(int i=0;i<64;++i)views.push_back(&packet);const auto aliases=JoinApplicationOperations(phase,views,1,true,true);
  auto conflict=packet;conflict.runner.requests[0]++;auto bad=views;bad.push_back(&conflict);
  require(joined.supported && aliases.supported && aliases.requested==joined.requested && !JoinApplicationOperations(phase,bad,1,true,true).supported &&
    !JoinApplicationOperations(phase,views,2,true,true).supported && !JoinApplicationOperations(phase,views,1,false,true).supported && !JoinApplicationOperations(phase,views,1,true,false).supported,"application identity/coverage/source-count controls");
  auto huge=packet;huge.actions[0].captured[0]=SIZE_MAX;huge.runner.requests[0]=1;std::vector<const ApplicationOperationStorage*> overflowing{&huge};
  require(JoinApplicationOperations(phase,overflowing,1,true,true).overflow,"application checked sum overflow");
  AllocatorSourcePhase other(NewRootStorageEpoch(),1);require(!JoinApplicationOperations(other,views,1,true,true).supported,"application mixed epochs rejected");
  phase.Close();require(!JoinApplicationOperations(phase,views,1,true,true).supported,"closed application phase rejected");
  size_t calls{};std::function<void()> unknown=[padding=std::array<char,1024>{},&calls](){++calls;(void)padding;};
  require(!InspectApplicationAction(unknown,&application,&create,&load,true).Supported() && !calls,"unknown action not invoked");
  return {{"schema","application-operation-storage.v1"},{"status","PASS"},{"captures",layout.captures},{"implementations",layout.implementations},{"inline_layout",{layout.function_inline,layout.runner_inline,layout.request_inline}},
    {"actions",cases},{"runner_cold",cold.requests},{"runner_active",active.requests},{"runner_failed",failed.requests},{"runner_cleared",cleared.requests},
    {"wrapper_overlap",joined.wrappers},{"additional",joined.additional},{"total",joined.requested},{"phase_owner_history_controls",true},{"unknown_action_not_invoked",true},
    {"scope","Six deferred/completion action shapes and ordinary-unique runner/request retained backing. Borrowed screens/Core/World and application shells are separate;outer widgets,queued I/O/external execution/thread/CRT/GPU/process remain open."}};
}
} // namespace cutum
