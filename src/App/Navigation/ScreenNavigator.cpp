#include "App/Navigation/ScreenNavigator.h"

#include "App/Application.h"
#include "App/Platform/CursorCapture.h"
#include "App/Settings/AppState.h"
#include "Game/GameSession.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Screens/LoadWorldScreen.h"
#include "Gui/Screens/MainMenuScreen.h"
#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Screens/SettingsScreen.h"
#include "Gui/Screens/WorldResourcePacksScreen.h"
#ifndef __ANDROID__
#include "App/Platform/WindowManager.h"
#endif

namespace cutum
{

namespace
{

#ifndef __ANDROID__
UWindowManager *GetWindowManager(GLFWwindow *window)
{
  if (!window)
  {
    return nullptr;
  }
  return static_cast<UWindowManager *>(glfwGetWindowUserPointer(window));
}
#endif

} // namespace

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

void UScreenNavigator::ShowSettings()
{
  if (!Application)
  {
    return;
  }
  Application->ConsoleOpen = false;
  Application->SuppressConsoleToggleChar = false;
  Application->PaletteOpen = false;
  Application->FreeCursor = false;
  Application->MainMenuScreen = nullptr;
  Application->MenuSubview = MenuSubview::Settings;
  Application->GuiContext->SetScreen(
      std::make_unique<USettingsScreen>(Application));
  Application->SyncCursorVisibility();
}

void UScreenNavigator::ShowWorldSettings()
{
  if (!Application || !Application->HasWorldSession())
  {
    return;
  }
  Application->ConsoleOpen = false;
  Application->SuppressConsoleToggleChar = false;
  Application->PaletteOpen = false;
  Application->FreeCursor = false;
  Application->MainMenuScreen = nullptr;
  Application->MenuSubview = MenuSubview::WorldSettings;
  Application->GuiContext->SetScreen(std::make_unique<UWorldResourcePacksScreen>(
      Application, [this]() { ShowMainMenu(); }));
  Application->SyncCursorVisibility();
}

void UScreenNavigator::ShowNewWorld()
{
  if (!Application)
  {
    return;
  }
  Application->ConsoleOpen = false;
  Application->SuppressConsoleToggleChar = false;
  Application->PaletteOpen = false;
  Application->FreeCursor = false;
  Application->MainMenuScreen = nullptr;
  Application->MenuSubview = MenuSubview::NewWorld;
  Application->GuiContext->SetScreen(
      std::make_unique<UNewWorldScreen>(Application));
  Application->SyncCursorVisibility();
}

void UScreenNavigator::ShowLoadWorld()
{
  if (!Application)
  {
    return;
  }
  Application->ConsoleOpen = false;
  Application->SuppressConsoleToggleChar = false;
  Application->PaletteOpen = false;
  Application->FreeCursor = false;
  Application->MainMenuScreen = nullptr;
  Application->MenuSubview = MenuSubview::LoadWorld;
  Application->GuiContext->SetScreen(
      std::make_unique<ULoadWorldScreen>(Application));
  Application->SyncCursorVisibility();
}

void UScreenNavigator::ReturnToMainMenu()
{
  if (!Application)
  {
    return;
  }
  if (Application->State == AppState::InGame)
  {
    if (Application->GameSession)
    {
      Application->GameSession->SaveCommandHistory();
    }
#ifndef __ANDROID__
    if (auto *wm = GetWindowManager(Application->Window))
    {
      wm->ResetGameplayMouseCapture();
    }
#endif
    Application->GuiContext->ClearInputState();
#ifndef __ANDROID__
    ReleasePlatformCursorClip();
#endif
  }
  Application->ConsoleOpen = false;
  Application->SuppressConsoleToggleChar = false;
  Application->PaletteOpen = false;
  Application->FreeCursor = false;
  Application->State = AppState::MainMenu;
  ShowMainMenu();
}

void UScreenNavigator::CloseInventoryPalette()
{
  if (!Application)
  {
    return;
  }
  Application->PaletteOpen = false;
  if (Application->PaletteScreen)
  {
    Application->PaletteScreen->SetVisible(false);
  }
  Application->GuiContext->ClearInputState();
  Application->SyncCursorVisibility();
}

void UScreenNavigator::CloseConsoleOverlay()
{
  if (!Application)
  {
    return;
  }
  Application->ConsoleOpen = false;
  Application->SuppressConsoleToggleChar = false;
  if (Application->ConsoleScreen)
  {
    Application->ConsoleScreen->SetVisible(false);
  }
  Application->ClearGameplayKeyboard();
  Application->GuiContext->ClearInputState();
  Application->SyncCursorVisibility();
}

void UScreenNavigator::CloseWorldGenOverlay()
{
  if (!Application)
  {
    return;
  }
  Application->WorldGenOpen = false;
  if (Application->WorldGenScreen)
  {
    Application->WorldGenScreen->SetVisible(false);
  }
  Application->GuiContext->ClearInputState();
  Application->SyncCursorVisibility();
}

} // namespace cutum
