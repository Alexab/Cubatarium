#include "App/Input/InputRouter.h"

#include "App/Application.h"
#include "App/Settings/AppState.h"
#include "Game/GameSession.h"
#include "Game/ModePolicy.h"
#include "Game/WorldGameMode.h"
#include "Game/Inventory/HotbarInput.h"
#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Screens/ConsoleScreen.h"
#include "Gui/Screens/MainMenuScreen.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Render/Camera/Camera.h"
#include "Render/Engine/GeometryEngine.h"
#include "World/Core/World.h"
#ifndef __ANDROID__
#include "App/Platform/CursorCapture.h"
#include "App/Platform/WindowManager.h"
#include <GLFW/glfw3.h>
#else
#include "App/Platform/GlfwKeyCompat.h"
#endif
#include <cctype>

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

bool KeyNameIs(const std::string &Name, int glfwKey)
{
  if (Name == "grave")
  {
    return glfwKey == GLFW_KEY_GRAVE_ACCENT;
  }
  if (Name.size() == 1)
  {
    const char c = static_cast<char>(std::tolower(Name[0]));
    if (c >= 'a' && c <= 'z')
    {
      return glfwKey == GLFW_KEY_A + (c - 'a');
    }
  }
  return false;
}

} // namespace

GuiKeyEvent UInputRouter::MakeGuiKeyEvent(int key, int action, int mods)
{
  GuiKeyEvent event;
  event.KeyCode = key;
  event.Action = action == GLFW_REPEAT
                     ? GuiKeyAction::Repeat
                     : (action == GLFW_PRESS ? GuiKeyAction::Press
                                             : GuiKeyAction::Release);
  event.Mods = mods;
  return event;
}

bool UInputRouter::RouteKey(UApplication &app, int key, int action, int mods)
{

  GuiKeyEvent event = MakeGuiKeyEvent(key, action, mods);

  if (action == GLFW_PRESS && app.State == AppState::InGame)
  {
    if (key == GLFW_KEY_ESCAPE)
    {
      if (app.ConsoleOpen && app.ConsoleScreen && app.ConsoleScreen->IsPopupOpen())
      {
        if (app.OverlayPopup)
        {
          app.OverlayPopup->Close();
        }
        return true;
      }
      if (app.ConsoleOpen)
      {
        app.ScreenNav.CloseConsoleOverlay();
        return true;
      }
      if (app.PaletteOpen)
      {
        app.ScreenNav.CloseInventoryPalette();
        return true;
      }
      if (app.WorldGenOpen)
      {
        app.ScreenNav.CloseWorldGenOverlay();
        return true;
      }
      if (app.CharacterSheetOpen)
      {
        app.CharacterSheetOpen = false;
        if (app.CharacterSheetScreen)
        {
          app.CharacterSheetScreen->SetVisible(false);
        }
        app.GuiContext->ClearInputState();
        app.SyncCursorVisibility();
        return true;
      }
      app.ReturnToMainMenu();
      return true;
    }
    if (!app.ConsoleOpen && key == GLFW_KEY_LEFT_ALT)
    {
#ifndef __ANDROID__
      app.FreeCursor = !app.FreeCursor;
      if (app.FreeCursor)
      {
        if (auto *wm = GetWindowManager(app.Window))
        {
          wm->ResetGameplayMouseCapture();
        }
        if (app.World && app.Window)
        {
          double x = 0.0;
          double y = 0.0;
          glfwGetCursorPos(app.Window, &x, &y);
          double fb_x = x;
          double fb_y = y;
          CursorWindowToFramebuffer(app.Window, x, y, fb_x, fb_y);
          if (auto camera = app.World->GetCurrentUserCamera())
          {
            camera->ResetMouseMove(fb_x, fb_y);
          }
        }
      }
      else
      {
        app.RecaptureMouseForLook();
      }
      app.SyncCursorVisibility();
#endif
      return true;
    }
    if (KeyNameIs(app.Ui.ConsoleKey, key))
    {
      app.ConsoleOpen = !app.ConsoleOpen;
#if defined(__ANDROID__)
      if (app.ConsoleOpen && app.HudScreen)
      {
        app.HudScreen->ReleaseTouchCaptures();
      }
#endif
      if (app.ConsoleScreen)
      {
        app.ConsoleScreen->SetVisible(app.ConsoleOpen);
      }
      if (app.ConsoleOpen)
      {
        app.ClearGameplayKeyboard();
        app.SuppressConsoleToggleChar = true;
      }
      else
      {
        app.SuppressConsoleToggleChar = false;
        app.GuiContext->ClearInputState();
      }
      app.SyncCursorVisibility();
      return true;
    }
    if (!app.ConsoleOpen && key == GLFW_KEY_F5 && app.World)
    {
      if (auto cam = app.World->GetCurrentUserCamera())
      {
        cam->CyclePerspective();
        if (app.Geometry)
        {
          app.Geometry->ShowTransientMessage(
              cam->GetViewController().ViewLabel(*cam), 1.5);
        }
      }
      return true;
    }
    if (!app.ConsoleOpen && app.World &&
        (key == GLFW_KEY_Q || key == GLFW_KEY_E))
    {
      if (auto cam = app.World->GetCurrentUserCamera())
      {
        if (cam->IsIsometricProjection())
        {
          cam->SnapIsoCameraYaw(key == GLFW_KEY_Q ? -1 : 1);
          return true;
        }
      }
    }
    if (!app.ConsoleOpen && KeyNameIs(app.Ui.PaletteKey, key))
    {
      if (!app.World ||
          !ModePolicy::AllowsCreativePalette(app.World->GetGameMode()))
      {
        if (app.Geometry)
        {
          app.Geometry->ShowTransientMessage(
              "Creative palette is not available in Survival mode", 2.5);
        }
        return true;
      }
      const bool sameTabOpen = app.PaletteOpen && app.PaletteScreen &&
                               app.PaletteScreen->GetActiveMainTab() == 0;
      if (sameTabOpen)
      {
        app.PaletteOpen = false;
        if (app.PaletteScreen)
        {
          app.PaletteScreen->SetVisible(false);
        }
        app.GuiContext->ClearInputState();
      }
      else
      {
        app.PaletteOpen = true;
        app.WorldGenOpen = false;
        if (app.WorldGenScreen)
        {
          app.WorldGenScreen->SetVisible(false);
        }
        if (app.PaletteScreen)
        {
          app.PaletteScreen->OpenWithMainTab(0);
        }
      }
#if defined(__ANDROID__)
      if (app.PaletteOpen && app.HudScreen)
      {
        app.HudScreen->ReleaseTouchCaptures();
      }
#endif
      app.SyncCursorVisibility();
      return true;
    }
    if (!app.ConsoleOpen && KeyNameIs(app.Ui.InventoryKey, key))
    {
      const bool allowCreativePalette = app.World &&
                                         ModePolicy::AllowsCreativePalette(
                                             app.World->GetGameMode());

      // Survival mode: InventoryKey toggles backpack UI.
      if (!allowCreativePalette)
      {
        if (app.SurvivalInventoryOpen)
        {
          app.SurvivalInventoryOpen = false;
          if (app.SurvivalInventoryScreen)
          {
            app.SurvivalInventoryScreen->SetVisible(false);
          }
          app.GuiContext->ClearInputState();
        }
        else
        {
          app.SurvivalInventoryOpen = true;
          app.PaletteOpen = false;
          app.WorldGenOpen = false;
          if (app.PaletteScreen)
          {
            app.PaletteScreen->SetVisible(false);
          }
          if (app.WorldGenScreen)
          {
            app.WorldGenScreen->SetVisible(false);
          }
          if (app.SurvivalInventoryScreen)
          {
            app.SurvivalInventoryScreen->SetVisible(true);
          }
        }
        app.SyncCursorVisibility();
        return true;
      }

      // Creative mode: InventoryKey toggles the creative palette.
      if (app.PaletteOpen)
      {
        app.PaletteOpen = false;
        if (app.PaletteScreen)
        {
          app.PaletteScreen->SetVisible(false);
        }
        app.GuiContext->ClearInputState();
      }
      else
      {
        app.PaletteOpen = true;
        app.WorldGenOpen = false;
        if (app.WorldGenScreen)
        {
          app.WorldGenScreen->SetVisible(false);
        }
        if (app.PaletteScreen)
        {
          // Restore last main tab (default Blocks on first open).
          app.PaletteScreen->SetVisible(true);
        }
      }
#if defined(__ANDROID__)
      if (app.PaletteOpen && app.HudScreen)
      {
        app.HudScreen->ReleaseTouchCaptures();
      }
#endif
      app.SyncCursorVisibility();
      return true;
    }
    if (!app.ConsoleOpen && KeyNameIs(app.Ui.WorldGenKey, key))
    {
      app.WorldGenOpen = !app.WorldGenOpen;
      if (app.WorldGenOpen)
      {
        app.PaletteOpen = false;
        app.CharacterSheetOpen = false;
        if (app.PaletteScreen)
        {
          app.PaletteScreen->SetVisible(false);
        }
        if (app.CharacterSheetScreen)
        {
          app.CharacterSheetScreen->SetVisible(false);
        }
      }
      else
      {
        app.GuiContext->ClearInputState();
      }
      if (app.WorldGenScreen)
      {
        app.WorldGenScreen->SetVisible(app.WorldGenOpen);
      }
      app.SyncCursorVisibility();
      return true;
    }
    if (!app.ConsoleOpen && KeyNameIs(app.Ui.CharacterKey, key))
    {
      app.CharacterSheetOpen = !app.CharacterSheetOpen;
      if (app.CharacterSheetOpen)
      {
        app.PaletteOpen = false;
        app.WorldGenOpen = false;
        if (app.PaletteScreen)
        {
          app.PaletteScreen->SetVisible(false);
        }
        if (app.WorldGenScreen)
        {
          app.WorldGenScreen->SetVisible(false);
        }
      }
      else
      {
        app.GuiContext->ClearInputState();
      }
      if (app.CharacterSheetScreen)
      {
        app.CharacterSheetScreen->SetVisible(app.CharacterSheetOpen);
      }
      app.SyncCursorVisibility();
      return true;
    }
    if (app.ConsoleOpen && key == GLFW_KEY_ENTER && app.ConsoleScreen)
    {
      app.ConsoleScreen->SubmitCommand();
      return true;
    }
    if (!app.ConsoleOpen)
    {
      const int hotbarSlot = PrimaryHotbarIndexFromGlfwKey(key);
      if (hotbarSlot >= 0 && app.GameSession)
      {
        if ((mods & GLFW_MOD_ALT) != 0)
        {
          return true;
        }
        app.GameSession->OnPrimaryHotbarKey(hotbarSlot);
        return true;
      }
    }
  }

  if (action == GLFW_PRESS && app.State == AppState::MainMenu &&
      key == GLFW_KEY_ESCAPE)
  {
    if (app.MainMenuScreen && app.MainMenuScreen->IsQuitConfirmationVisible())
    {
      app.MainMenuScreen->ShowQuitConfirmation(false);
      return true;
    }
    if (app.MenuSubview != MenuSubview::Main)
    {
      app.ScreenNav.ShowMainMenu();
      return true;
    }
    if (app.HasWorldSession())
    {
      app.RequestEnterGame();
      return true;
    }
    if (app.MainMenuScreen)
    {
      app.MainMenuScreen->ShowQuitConfirmation(true);
      return true;
    }
  }

  if (app.State == AppState::InGame && app.ConsoleOpen && app.ConsoleScreen)
  {
    if (KeyNameIs(app.Ui.ConsoleKey, key))
    {
      return true;
    }
    app.ConsoleScreen->RouteKey(event);
    return true;
  }

  if (app.GuiContext->RouteKey(event))
  {
    return true;
  }
  return false;

}

bool UInputRouter::RouteChar(UApplication &app, unsigned int codepoint)
{

  if (app.State == AppState::InGame && app.ConsoleOpen && app.ConsoleScreen)
  {
    if (app.SuppressConsoleToggleChar)
    {
      app.SuppressConsoleToggleChar = false;
      return true;
    }
    app.ConsoleScreen->RouteChar(GuiCharEvent{codepoint});
    return true;
  }
  app.SuppressConsoleToggleChar = false;
  return app.GuiContext->RouteChar(GuiCharEvent{codepoint});

}

bool UInputRouter::RouteMouseButton(UApplication &app, int button, bool pressed, int x, int y, int pointer_id)
{

  GuiMouseEvent event;
  event.X = x;
  event.Y = y;
  event.PointerId = pointer_id;
  event.Button = button == GLFW_MOUSE_BUTTON_RIGHT    ? GuiMouseButton::Right
                 : button == GLFW_MOUSE_BUTTON_MIDDLE ? GuiMouseButton::Middle
                                                      : GuiMouseButton::Left;
  event.Pressed = pressed;

  if (app.PaletteOpen && app.PaletteScreen && event.Button == GuiMouseButton::Left)
  {
    app.PaletteScreen->SetPointerPressed(pressed);
  }
  if (app.WorldGenOpen && app.WorldGenScreen && event.Button == GuiMouseButton::Left)
  {
    app.WorldGenScreen->SetPointerPressed(pressed);
  }

  if (app.State == AppState::InGame)
  {
    app.DragCursorX = x;
    app.DragCursorY = y;
    if (event.Button == GuiMouseButton::Left && !pressed)
    {
      if ((app.GameSession && app.GameSession->IsDragging()) ||
          app.OverlayPressedWidget || app.HasAnyOverlayCapture())
      {
        app.FinishInventoryPointerGesture(event);
        return true;
      }
    }
    if ((app.OverlayPopup && app.OverlayPopup->IsOpen()) || app.ConsoleOpen)
    {
      if (app.ConsoleScreen &&
          app.ConsoleScreen->RouteMouseButton(event, app.GuiContext->GetRenderer()))
      {
        return true;
      }
    }
    if (event.Button == GuiMouseButton::Left)
    {
      if (app.TryRouteInGameOverlay(event, pressed))
      {
        return true;
      }
      if (app.FreeCursor && pressed)
      {
        app.RecaptureMouseForLook();
        return true;
      }
    }
    return false;
  }

  return pressed ? app.GuiContext->RouteMouseDown(event)
                 : app.GuiContext->RouteMouseUp(event);

}

bool UInputRouter::RouteMouseMove(UApplication &app, int x, int y, int pointer_id)
{

  GuiMouseEvent event;
  event.X = x;
  event.Y = y;
  event.PointerId = pointer_id;
  if (app.State == AppState::InGame && app.HudScreen)
  {
    app.HudScreen->SetPointerPosition(x, y);
  }
  if (app.PaletteOpen && app.PaletteScreen)
  {
    app.PaletteScreen->SetPointerPosition(x, y);
  }
  if (app.WorldGenOpen && app.WorldGenScreen)
  {
    app.WorldGenScreen->SetPointerPosition(x, y);
  }
  if (app.State == AppState::InGame)
  {
    app.DragCursorX = x;
    app.DragCursorY = y;
    // Deliver move to the pressed overlay leaf so GuiSlot can BeginDrag.
    if (app.OverlayPressedWidget &&
        !(app.GameSession && app.GameSession->IsDragging()))
    {
      app.OverlayPressedWidget->OnMouseMove(event);
      return true;
    }
    if (app.GameSession && app.GameSession->IsDragging())
    {
      return true;
    }
    const int pointerIndex = app.NormalizeOverlayPointer(pointer_id);
    const UApplication::OverlayPointerCapture capture = app.OverlayCaptures[pointerIndex];
    if (capture != UApplication::OverlayPointerCapture::None)
    {
      auto routeCapturedMove = [&](UGuiWidget *root) -> bool
      { return root && root->OnMouseMove(event); };
      switch (capture)
      {
      case UApplication::OverlayPointerCapture::Palette:
        if (app.PaletteOpen && routeCapturedMove(app.PaletteScreen->GetRoot()))
        {
          return true;
        }
        break;
      case UApplication::OverlayPointerCapture::Console:
        if (app.ConsoleOpen && routeCapturedMove(app.ConsoleScreen->GetRoot()))
        {
          return true;
        }
        break;
      case UApplication::OverlayPointerCapture::WorldGen:
        if (app.WorldGenOpen && routeCapturedMove(app.WorldGenScreen->GetRoot()))
        {
          return true;
        }
        break;
      case UApplication::OverlayPointerCapture::Hud:
        if (routeCapturedMove(app.HudScreen ? app.HudScreen->GetRoot() : nullptr))
        {
          return true;
        }
        break;
      case UApplication::OverlayPointerCapture::CharacterSheet:
        if (app.CharacterSheetOpen && app.CharacterSheetScreen &&
            routeCapturedMove(app.CharacterSheetScreen->GetRoot()))
        {
          return true;
        }
        break;
      default:
        break;
      }
    }
    if (app.OverlayPopup && app.OverlayPopup->IsOpen())
    {
      if (app.OverlayPopup->OnMouseMove(event))
      {
        return true;
      }
    }
    if (app.ConsoleOpen && app.ConsoleScreen &&
        app.ConsoleScreen->RouteMouseMove(event, app.GuiContext->GetRenderer()))
    {
      return true;
    }
#if defined(__ANDROID__)
    if (app.HudScreen && app.HudScreen->RouteTouchMove(pointer_id, x, y))
    {
      return true;
    }
#endif
    auto routeMove = [&](UGuiWidget *root) -> bool
    { return root && root->OnMouseMove(event); };
    bool handled = false;
    if (app.PaletteOpen)
    {
      handled |= routeMove(app.PaletteScreen->GetRoot());
    }
    if (app.WorldGenOpen)
    {
      handled |= routeMove(app.WorldGenScreen->GetRoot());
    }
    if (app.CharacterSheetOpen && app.CharacterSheetScreen)
    {
      handled |= routeMove(app.CharacterSheetScreen->GetRoot());
    }
    handled |= routeMove(app.HudScreen ? app.HudScreen->GetRoot() : nullptr);
    return handled;
  }
  return app.GuiContext->RouteMouseMove(event);

}

bool UInputRouter::RouteScroll(UApplication &app, double xoffset, double yoffset, int mouse_x, int mouse_y)
{

  if (app.State == AppState::InGame)
  {
    GuiScrollEvent event{xoffset, yoffset};
    auto routeScroll = [&](UGuiWidget *root) -> bool
    { return root && root->ScrollAtPoint(mouse_x, mouse_y, event); };
    if (app.WorldGenOpen &&
        routeScroll(app.WorldGenScreen ? app.WorldGenScreen->GetRoot() : nullptr))
    {
      return true;
    }
    if (app.PaletteOpen &&
        routeScroll(app.PaletteScreen ? app.PaletteScreen->GetRoot() : nullptr))
    {
      return true;
    }
    if (app.ConsoleOpen &&
        routeScroll(app.ConsoleScreen ? app.ConsoleScreen->GetRoot() : nullptr))
    {
      return true;
    }
    if (routeScroll(app.HudScreen ? app.HudScreen->GetRoot() : nullptr))
    {
      return true;
    }
    // Perspective: Minecraft-style hotbar cycle. Isometric keeps camera zoom.
    if (app.GameSession && app.World)
    {
      if (auto camera = app.World->GetCurrentUserCamera())
      {
        if (!camera->IsIsometricProjection() && yoffset != 0.0)
        {
          constexpr size_t kSlots = 10;
          const size_t current = app.GameSession->GetSelectedSlot(0);
          const int delta = yoffset > 0.0 ? -1 : 1;
          const size_t next =
              static_cast<size_t>((static_cast<int>(current) + delta +
                                   static_cast<int>(kSlots)) %
                                  static_cast<int>(kSlots));
          app.GameSession->SelectSlot(0, next);
          return true;
        }
      }
    }
    return false;
  }
  return app.GuiContext->RouteScroll(GuiScrollEvent{xoffset, yoffset}, mouse_x,
                                  mouse_y);

}

} // namespace cutum
