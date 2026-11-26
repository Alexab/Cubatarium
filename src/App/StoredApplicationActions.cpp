#include "App/StoredApplicationActions.h"
#include "App/Application.h"
#include "App/Core.h"
#include "Game/GameSession.h"
namespace cutum {
void ApplicationReturnMenuAction::operator()()const {owner->ReturnToMainMenu();}
void ApplicationShowMenuAction::operator()()const {owner->ShowMainMenu();}
void ApplicationRetrySaveAction::operator()()const {
  WorldRunnerRequest retry;retry.op=WorldRunnerOp::Save;retry.saveConfigAfter=false;owner->BeginWorldOperation(std::move(retry));
}
void ApplicationScheduleRetrySaveAction::operator()() const
{
  owner->ScheduleDeferredMenuAction(ApplicationRetrySaveAction{owner});
}
void ApplicationScheduleShowMenuAction::operator()() const
{
  owner->ScheduleDeferredMenuAction(ApplicationShowMenuAction{owner});
}
void ApplicationShutdownCompleteAction::operator()()const {
  if(owner->GameSession)owner->GameSession->SaveCommandHistory();
  if(owner->Core)owner->Core->SaveConfigFile();
  if(owner->ShutdownCloseAfter)owner->RequestQuit();
}
} // namespace cutum
