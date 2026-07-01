#include "App/Navigation/ScreenNavigator.h"

#include "App/Application.h"
#include "App/Settings/AppState.h"
#include "Game/GameSession.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Screens/MainMenuScreen.h"

namespace cutum
{

UScreenNavigator::UScreenNavigator(UApplication *application)
    : Application(application)
{
}

void UScreenNavigator::ShowMainMenu()
{
  if (!Application)
  {
    return;
  }
  Application->ProgressScreen = nullptr;
  Application->ConsoleOpen = false;
  Application->SuppressConsoleToggleChar = false;
  Application->PaletteOpen = false;
  Application->FreeCursor = false;
  auto menu = std::make_unique<UMainMenuScreen>(Application->GameSession.get());
  Application->MainMenuScreen = menu.get();
  Application->MenuSubview = MenuSubview::Main;
  Application->GuiContext->SetScreen(std::move(menu));
  Application->SyncCursorVisibility();
}

} // namespace cutum
