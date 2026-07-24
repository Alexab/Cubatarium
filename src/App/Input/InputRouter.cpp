#include "App/Input/InputRouter.h"

#include "App/Application.h"
#include "App/Settings/AppState.h"
#include "Game/GameSession.h"
#include "Game/Inventory/HotbarInput.h"
#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Screens/ConsoleScreen.h"
#include "Gui/Screens/MainMenuScreen.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraPerspective.h"
#include "Render/Engine/GeometryEngine.h"
#include "World/Core/World.h"
#ifndef __ANDROID__
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
          if (auto camera = app.World->GetCurrentUserCamera())
          {
            camera->ResetMouseMove(x, y);
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
              CameraPerspectiveLabel(cam->GetPerspective()), 1.5);
        }
      }
      return true;
    }
    if (!app.ConsoleOpen && KeyNameIs(app.Ui.PaletteKey, key))
    {
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
        if (app.PaletteScreen)
        {
          app.PaletteScreen->SetVisible(false);
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
    if (event.Button == GuiMouseButton::Left && !pressed && app.GameSession &&
        app.GameSession->IsDragging())
    {
      SlotAddress target;
      const bool hasTarget = app.ResolveSlotAt(x, y, target);
      if (hasTarget)
      {
        if (!app.GameSession->DropOnSlot(target))
        {
          app.GameSession->CancelDrag();
        }
      }
      else
      {
        app.GameSession->CancelDrag();
      }
      if (app.HasAnyOverlayCapture())
      {
        app.TryRouteInGameOverlay(event, false);
      }
      return true;
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
    return false;
  }
  return app.GuiContext->RouteScroll(GuiScrollEvent{xoffset, yoffset}, mouse_x,
                                  mouse_y);

}

} // namespace cutum
