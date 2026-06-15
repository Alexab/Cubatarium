#include "App/Application.h"
#include "Game/Inventory/HotbarInput.h"
#include "Game/Inventory/SlotInteraction.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraPerspective.h"
#include "App/Core.h"
#include "App/Platform/CursorCapture.h"
#ifdef __ANDROID__
#include "App/Platform/TouchInputBridge.h"
#endif
#ifndef __ANDROID__
#include "App/Platform/WindowManager.h"
#endif
#include "Game/GameSession.h"
#include "Render/Engine/GeometryEngine.h"
#include "Gui/Cache/CreatureIconCache.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiIconSource.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTypes.h"
#include "Gui/Interfaces/IGuiClipboard.h"
#include "Gui/Interfaces/IGuiIconSource.h"
#include "Gui/Cache/PrefabIconCache.h"
#include "Gui/Screens/ConsoleScreen.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Screens/InGameHudScreen.h"
#include "Gui/Screens/LoadWorldScreen.h"
#include "Gui/Screens/MainMenuScreen.h"
#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Screens/SettingsScreen.h"
#include "Gui/Widgets/GuiPopupMenu.h"
#include "Gui/Widgets/GuiWidget.h"
#include "World/Prefabs/Prefab.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/TextRenderer.h"
#include "Render/Textures/TextureCube.h"
#include "Creatures/Player/User.h"
#include "Render/Engine/ViewEngine.h"
#include "World/Core/World.h"

#ifndef __ANDROID__
#include <GLFW/glfw3.h>
#else
#include "App/Platform/GlfwKeyCompat.h"
#endif
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

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

class GlfwClipboard : public IGuiClipboard
{
public:
  explicit GlfwClipboard(GLFWwindow *window) : Window(window) {}

  std::string GetString() const override
  {
    if (!Window)
    {
      return {};
    }
    const char *text = glfwGetClipboardString(Window);
    return text ? std::string(text) : std::string{};
  }

  void SetString(const std::string &text) override
  {
    if (Window)
    {
      glfwSetClipboardString(Window, text.c_str());
    }
  }

private:
  GLFWwindow *Window;
};
#else
class NullClipboard : public IGuiClipboard
{
public:
  std::string GetString() const override { return {}; }
  void SetString(const std::string &) override {}
};
#endif

bool KeyNameIs(const std::string &name, int glfwKey)
{
  if (name == "grave")
  {
    return glfwKey == GLFW_KEY_GRAVE_ACCENT;
  }
  if (name.size() == 1)
  {
    const char c = static_cast<char>(std::tolower(name[0]));
    if (c >= 'a' && c <= 'z')
    {
      return glfwKey == GLFW_KEY_A + (c - 'a');
    }
  }
  return false;
}

} // namespace

UApplication::UApplication(
    std::shared_ptr<UCore> core, std::shared_ptr<UWorld> world,
    std::shared_ptr<UGeometryEngine> geometry,
    std::shared_ptr<UViewEngine> views,
    std::shared_ptr<UTextRenderer> text_renderer,
    std::shared_ptr<UShaderManager> shader_manager,
    std::shared_ptr<UBlockDefinitionStorage> block_definitions)
    : Core(std::move(core)), World(std::move(world)),
      Geometry(std::move(geometry)), Views(std::move(views)),
      TextRenderer(std::move(text_renderer)),
      ShaderManager(std::move(shader_manager)),
      BlockDefinitions(std::move(block_definitions))
{
  GuiContext = std::make_unique<UGuiContext>();
  GameSession = std::make_unique<UGameSession>(this, World);
}

UApplication::~UApplication() = default;

void UApplication::Startup(const std::string &configPath)
{
  if (Core)
  {
    Core->LoadConfig(configPath);
    Ui = Core->GetUiSettings();
  }
  if (Geometry)
  {
    Geometry->SetShowHud(Ui.legacyHud);
    Geometry->SetShowPerformance(Ui.showPerformance);
  }
  if (!GuiContext->Initialize(ShaderManager, TextRenderer))
  {
    std::cerr << "Application: UGuiContext init failed" << std::endl;
    return;
  }
  if (Window)
  {
#ifndef __ANDROID__
    int fbW = 0;
    int fbH = 0;
    glfwGetFramebufferSize(Window, &fbW, &fbH);
    if (fbW > 0 && fbH > 0 && TextRenderer)
    {
      TextRenderer->SetWindowSize(fbW, fbH);
    }
#endif
  }
  if (Core && BlockDefinitions)
  {
    const std::string typesPath = "content/types.json";
    GameSession->InitializeCatalog(typesPath, *BlockDefinitions,
                                   *Core->GetPrefabLibrary());
  }
  GameSession->RegisterCommands();

  if (Core && BlockDefinitions && ShaderManager)
  {
    auto textures = Core->GetTextureCubeStorage();
    auto prefabCache = std::make_unique<UPrefabIconCache>(
        Core->GetPrefabLibrary(), textures, BlockDefinitions, ShaderManager);
    if (prefabCache->Initialize())
    {
      std::unique_ptr<UCreatureIconCache> creatureCache;
      if (World)
      {
        creatureCache = std::make_unique<UCreatureIconCache>(
            World->GetCreatureDefinitionStorage(),
            World->GetSkinDefinitionStorage(),
            Core->GetCreatureTextureStorage(), ShaderManager);
        if (!creatureCache->Initialize())
        {
          creatureCache.reset();
        }
      }
      IconSource = std::make_unique<UGuiIconSource>(
          textures, std::move(prefabCache), std::move(creatureCache));
    }
  }

  State = AppState::MainMenu;
  ShowMainMenu();
}

void UApplication::ScheduleEnterGame() { PendingEnterGame = true; }

void UApplication::ScheduleQuit() { PendingQuit = true; }

void UApplication::RequestEnterGame()
{
  if (State == AppState::InGame)
  {
    return;
  }

  if (!WorldSessionActive)
  {
    State = AppState::Loading;
    if (Core)
    {
      Core->EnterGame();
    }
    if (World)
    {
      if (auto user = World->GetCurrentUser())
      {
        World->EnsurePlayerHotbarCount(user,
                                       static_cast<size_t>(Ui.hotbarCount));
      }
    }
    WorldSessionActive = true;
    ShowInGameHud();
  }
  else
  {
    GuiContext->SetScreen(nullptr);
  }

  State = AppState::InGame;
  EnterInGameInputState();
}

void UApplication::RequestQuit()
{
  if (GameSession)
  {
    GameSession->SaveCommandHistory();
  }
  QuitRequested = true;
#ifndef __ANDROID__
  if (Window)
  {
    glfwSetWindowShouldClose(Window, GLFW_TRUE);
  }
#endif
}

void UApplication::ShowMainMenu()
{
  ConsoleOpen = false;
  SuppressConsoleToggleChar = false;
  PaletteOpen = false;
  FreeCursor = false;
  auto menu = std::make_unique<UMainMenuScreen>(GameSession.get());
  MainMenuScreen = menu.get();
  MenuSubview = MenuSubview::Main;
  GuiContext->SetScreen(std::move(menu));
  SyncCursorVisibility();
}

void UApplication::SetHotbarCountSetting(int count)
{
  Ui.hotbarCount = std::clamp(count, 1, 2);
  if (Core)
  {
    AppSettingsSnapshot app = Core->GetAppSettings();
    app.Ui = Ui;
    Core->ApplyAppSettings(app);
    Core->SaveConfigFile();
  }
  if (World)
  {
    if (auto user = World->GetCurrentUser())
    {
      World->EnsurePlayerHotbarCount(user, static_cast<size_t>(Ui.hotbarCount));
    }
  }
}

void UApplication::ReturnToMainMenu()
{
  if (State == AppState::InGame)
  {
    if (GameSession)
    {
      GameSession->SaveCommandHistory();
    }
#ifndef __ANDROID__
    if (auto *wm = GetWindowManager(Window))
    {
      wm->ResetGameplayMouseCapture();
    }
#endif
    GuiContext->ClearInputState();
#ifndef __ANDROID__
    ReleasePlatformCursorClip();
#endif
  }
  ConsoleOpen = false;
  SuppressConsoleToggleChar = false;
  PaletteOpen = false;
  FreeCursor = false;
  State = AppState::MainMenu;
  ShowMainMenu();
}

void UApplication::ShowSettings()
{
  ConsoleOpen = false;
  SuppressConsoleToggleChar = false;
  PaletteOpen = false;
  FreeCursor = false;
  MainMenuScreen = nullptr;
  MenuSubview = MenuSubview::Settings;
  GuiContext->SetScreen(std::make_unique<USettingsScreen>(this));
  SyncCursorVisibility();
}

void UApplication::ShowNewWorld()
{
  ConsoleOpen = false;
  SuppressConsoleToggleChar = false;
  PaletteOpen = false;
  FreeCursor = false;
  MainMenuScreen = nullptr;
  MenuSubview = MenuSubview::NewWorld;
  GuiContext->SetScreen(std::make_unique<UNewWorldScreen>(this));
  SyncCursorVisibility();
}

void UApplication::ShowLoadWorld()
{
  ConsoleOpen = false;
  SuppressConsoleToggleChar = false;
  PaletteOpen = false;
  FreeCursor = false;
  MainMenuScreen = nullptr;
  MenuSubview = MenuSubview::LoadWorld;
  GuiContext->SetScreen(std::make_unique<ULoadWorldScreen>(this));
  SyncCursorVisibility();
}

void UApplication::SaveActiveWorldIfNeeded()
{
  if (!WorldSessionActive || !Core)
  {
    return;
  }
  Core->SaveWorld(World->GetWorldName());
  Core->SaveConfigFile();
}

void UApplication::EnterGameAfterWorldChange()
{
  WorldSessionActive = true;
  if (Core && World)
  {
    Ui = Core->GetUiSettings();
    if (auto user = World->GetCurrentUser())
    {
      World->EnsurePlayerHotbarCount(user, static_cast<size_t>(Ui.hotbarCount));
    }
    if (Geometry)
    {
      Geometry->SetShowHud(Ui.legacyHud);
    }
    World->FinalizePlayerAfterWorldLoad();
  }
  ShowInGameHud();
  State = AppState::InGame;
  EnterInGameInputState();
}

void UApplication::ScheduleDeferredMenuAction(std::function<void()> action)
{
  PendingMenuAction = std::move(action);
}

void UApplication::SaveIfNeededAndProceed(std::function<void()> proceed)
{
  if (!proceed)
  {
    return;
  }
  SaveActiveWorldIfNeeded();
  ScheduleDeferredMenuAction(std::move(proceed));
}

AppSettingsSnapshot UApplication::LoadAppSettingsSnapshot() const
{
  return Core ? Core->GetAppSettings() : AppSettingsSnapshot{};
}

ProceduralSettings UApplication::LoadProceduralTemplate() const
{
  return Core ? Core->GetProceduralTemplate() : ProceduralSettings{};
}

void UApplication::SaveAppAndTemplateSettings(
    const AppSettingsSnapshot &app, const ProceduralSettings &procedural)
{
  if (!Core)
  {
    return;
  }
  Core->ApplyAppSettings(app);
  Core->SetProceduralTemplate(procedural);
  Core->SaveConfigFile();
  Ui = Core->GetUiSettings();
  if (Geometry)
  {
    Geometry->SetShowHud(Ui.legacyHud);
    Geometry->SetShowPerformance(Ui.showPerformance);
  }
  if (World)
  {
    if (auto user = World->GetCurrentUser())
    {
      World->EnsurePlayerHotbarCount(user, static_cast<size_t>(Ui.hotbarCount));
    }
  }
  SyncCursorVisibility();
}

void UApplication::CreateNewWorldWithSettings(
    const ProceduralSettings &settings)
{
  if (!Core)
  {
    return;
  }
  Core->SetProceduralTemplate(settings);
  Core->CreateNewWorldFromTemplate();
  Core->SaveConfigFile();
  EnterGameAfterWorldChange();
}

void UApplication::LoadSelectedWorld(const std::string &worldName)
{
  if (!Core)
  {
    return;
  }
  Core->LoadWorldByName(worldName);
  Core->SaveConfigFile();
  EnterGameAfterWorldChange();
}

void UApplication::RefreshWorldList()
{
  if (Core)
  {
    Core->RefreshWorldList();
  }
}

const std::vector<std::string> &UApplication::GetWorldNames() const
{
  static const std::vector<std::string> kEmpty;
  return Core ? Core->GetWorldList() : kEmpty;
}

void UApplication::ShowInGameHud()
{
#ifndef __ANDROID__
  if (Window && !Clipboard)
  {
    Clipboard = std::make_unique<GlfwClipboard>(Window);
    GuiContext->SetClipboard(Clipboard.get());
  }
#else
  if (!Clipboard)
  {
    Clipboard = std::make_unique<NullClipboard>();
    GuiContext->SetClipboard(Clipboard.get());
  }
#endif

  GameSession->InitCommandHistory(GetExecutableDirectory() /
                                  "console_history.txt");

  IGuiIconSource *icons = IconSource.get();
  auto hud = std::make_unique<UInGameHudScreen>(GameSession.get(),
                                                &GuiContext->GetTheme(), icons);
#if defined(__ANDROID__)
  if (touchBridge_)
  {
    hud->ConfigureTouchControls(
        touchBridge_, [this]() { ReturnToMainMenu(); },
        [this]() {
          PaletteOpen = !PaletteOpen;
          if (PaletteScreen)
          {
            PaletteScreen->SetVisible(PaletteOpen);
          }
          SyncCursorVisibility();
        },
        [this]() {
          ConsoleOpen = !ConsoleOpen;
          if (ConsoleScreen)
          {
            ConsoleScreen->SetVisible(ConsoleOpen);
          }
          if (ConsoleOpen)
          {
            ClearGameplayKeyboard();
          }
          SyncCursorVisibility();
        },
        [this]() { TryToggleFlightOnJumpPress(); });
  }
#endif
  hud->Build(*GuiContext);
  HudScreen = std::move(hud);

  OverlayPopup = std::make_unique<UGuiPopupMenu>(&GuiContext->GetTheme());

  ConsoleScreen = std::make_unique<UConsoleScreen>(GameSession.get());
  ConsoleScreen->Build(*GuiContext);
  ConsoleScreen->AttachPopup(OverlayPopup.get());
  ConsoleScreen->SetVisible(false);

  PaletteScreen = std::make_unique<UCreativePaletteScreen>(
      &GameSession->GetContentCatalog(), GameSession.get(), icons);
  PaletteScreen->Build(*GuiContext);
  PaletteScreen->SetVisible(false);

  GuiContext->SetScreen(nullptr);
}

bool UApplication::UsesUiPointer() const
{
  return State == AppState::MainMenu || FreeCursor || ConsoleOpen ||
         PaletteOpen;
}

bool UApplication::BlocksGameMouseLook() const
{
  return State == AppState::InGame &&
         (FreeCursor || ConsoleOpen || PaletteOpen);
}

AppCursorPolicy UApplication::GetCursorPolicy() const
{
  if (UsesUiPointer())
  {
    return AppCursorPolicy::Free;
  }
  if (State == AppState::InGame)
  {
    if (Ui.controlScheme == ControlScheme::Classic)
    {
      return AppCursorPolicy::CapturedHidden;
    }
    return AppCursorPolicy::ConfinedVisible;
  }
  return AppCursorPolicy::Free;
}

void UApplication::SyncCursorVisibility()
{
#ifndef __ANDROID__
  if (!Window)
  {
    return;
  }
  ApplyCursorPolicy(Window, GetCursorPolicy());
#endif
}

void UApplication::ClearGameplayKeyboard()
{
  if (!World)
  {
    return;
  }
  if (auto camera = World->GetCurrentUserCamera())
  {
    camera->ResetAllKeyStatus();
  }
}

void UApplication::HandleWindowFocus(bool focused)
{
#ifndef __ANDROID__
  if (!Window)
  {
    return;
  }
  if (!focused)
  {
    ReleasePlatformCursorClip();
    return;
  }
  SyncCursorVisibility();
#else
  (void)focused;
#endif
}

void UApplication::EnterInGameInputState()
{
  ConsoleOpen = false;
  SuppressConsoleToggleChar = false;
  PaletteOpen = false;
  FreeCursor = false;
#if defined(__ANDROID__)
  Ui.controlScheme = ControlScheme::Cubatarium;
#endif
  GuiContext->ClearInputState();
  SyncCursorVisibility();
#ifndef __ANDROID__
  if (auto *wm = GetWindowManager(Window))
  {
    wm->ResetGameplayMouseCapture();
  }
  if (World && Window)
  {
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(Window, &x, &y);
    if (auto camera = World->GetCurrentUserCamera())
    {
      camera->ResetMouseMove(x, y);
    }
  }
#endif
}

void UApplication::RecaptureMouseForLook()
{
  FreeCursor = false;
  EnterInGameInputState();
}

int UApplication::NormalizeOverlayPointer(int pointerId) const
{
  if (pointerId < 0)
  {
    return 0;
  }
  if (pointerId >= kMaxOverlayPointers)
  {
    return kMaxOverlayPointers - 1;
  }
  return pointerId;
}

bool UApplication::HasAnyOverlayCapture() const
{
  for (const OverlayPointerCapture capture : overlayCaptures_)
  {
    if (capture != OverlayPointerCapture::None)
    {
      return true;
    }
  }
  return false;
}

bool UApplication::TryRouteInGameOverlay(const GuiMouseEvent &event,
                                         bool pressed)
{
  const int pointerIndex = NormalizeOverlayPointer(event.pointerId);

  auto routeRoot = [&](UGuiWidget *root, bool requireHitTest) -> bool
  {
    if (!root)
    {
      return false;
    }
    if (requireHitTest && !root->HitTest(event.x, event.y))
    {
      return false;
    }
    return pressed ? root->OnMouseDown(event) : root->OnMouseUp(event);
  };

  if (pressed)
  {
    if (PaletteOpen && routeRoot(PaletteScreen->GetRoot(), true))
    {
      overlayCaptures_[pointerIndex] = OverlayPointerCapture::Palette;
      return true;
    }
    if (ConsoleOpen && routeRoot(ConsoleScreen->GetRoot(), true))
    {
      overlayCaptures_[pointerIndex] = OverlayPointerCapture::Console;
      return true;
    }
    if (routeRoot(HudScreen ? HudScreen->GetRoot() : nullptr, true))
    {
      overlayCaptures_[pointerIndex] = OverlayPointerCapture::Hud;
      return true;
    }
    return false;
  }

  const OverlayPointerCapture capture = overlayCaptures_[pointerIndex];
  overlayCaptures_[pointerIndex] = OverlayPointerCapture::None;
  if (capture == OverlayPointerCapture::None)
  {
    return false;
  }

  switch (capture)
  {
  case OverlayPointerCapture::Palette:
    return PaletteOpen && routeRoot(PaletteScreen->GetRoot(), false);
  case OverlayPointerCapture::Console:
    return ConsoleOpen && routeRoot(ConsoleScreen->GetRoot(), false);
  case OverlayPointerCapture::Hud:
    return routeRoot(HudScreen ? HudScreen->GetRoot() : nullptr, false);
  default:
    return false;
  }
}

bool UApplication::ResolveSlotAt(int x, int y, SlotAddress &out)
{
  // Хотбар под палитрой: при drop сначала проверяем HUD, иначе палитра
  // «съедает» цель.
  if (HudScreen && HudScreen->PickSlot(x, y, out))
  {
    return true;
  }
  if (PaletteOpen && PaletteScreen && PaletteScreen->PickSlot(x, y, out))
  {
    return true;
  }
  return false;
}

void UApplication::DrawDragGhost(int width, int height)
{
  if (!GameSession || !GameSession->IsDragging() || !IconSource || !GuiContext)
  {
    return;
  }
  const DragState &drag = GameSession->GetDrag();
  if (drag.entry.empty)
  {
    return;
  }
  GLuint tex = 0;
  switch (drag.entry.kind)
  {
  case InventoryEntryKind::Block:
    tex = IconSource->GetBlockIconTexture(drag.entry.id);
    break;
  case InventoryEntryKind::UObject:
    tex = IconSource->GetPrefabIconTexture(drag.entry.id);
    break;
  case InventoryEntryKind::UCreature:
    tex = IconSource->GetCreatureIconTexture(drag.entry.id);
    break;
  case InventoryEntryKind::Skin:
    tex = IconSource->GetSkinIconTexture(drag.entry.id);
    break;
  }
  if (tex == 0)
  {
    return;
  }
  const int size = GuiContext->GetTheme().hotbarSlotSize;
  const GuiRect rect{DragCursorX - size / 2, DragCursorY - size / 2, size,
                     size};
  UGuiRenderer &renderer = GuiContext->GetRenderer();
  renderer.BeginFrame(width, height);
  renderer.DrawTexturedRect(rect, tex);
  renderer.EndFrame();
}

void UApplication::Update(double dt)
{
  if (PendingEnterGame)
  {
    PendingEnterGame = false;
    RequestEnterGame();
  }
  if (PendingQuit)
  {
    PendingQuit = false;
    RequestQuit();
  }
  if (PendingMenuAction)
  {
    auto action = std::move(PendingMenuAction);
    PendingMenuAction = nullptr;
    action();
  }

  if (State == AppState::MainMenu)
  {
    GuiContext->Update(dt);
  }
  else if (State == AppState::InGame)
  {
    if (HudScreen)
    {
#ifndef __ANDROID__
      if (Window)
      {
        int fbW = 0;
        int fbH = 0;
        glfwGetFramebufferSize(Window, &fbW, &fbH);
        if (fbW > 0 && fbH > 0)
        {
          HudScreen->OnViewportChanged(fbW, fbH);
        }
      }
#endif
      HudScreen->Update(dt);
    }
    if (ConsoleScreen)
    {
      ConsoleScreen->Update(dt);
    }
    if (PaletteScreen)
    {
      PaletteScreen->Update(dt);
    }
  }
  SyncCursorVisibility();
}

void UApplication::ProcessInput() { (void)0; }

void UApplication::SetViewportInsets(int left, int top, int right, int bottom)
{
  viewportInsetLeft_ = std::max(0, left);
  viewportInsetTop_ = std::max(0, top);
  viewportInsetRight_ = std::max(0, right);
  viewportInsetBottom_ = std::max(0, bottom);
  if (Geometry)
  {
    Geometry->SetOverlayMargins(viewportInsetRight_ + 16, viewportInsetTop_ + 30);
  }
}

void UApplication::SetUiScale(float scale)
{
  if (std::fabs(scale - uiScale_) < 0.01f)
  {
    return;
  }
  uiScale_ = scale;
  if (GuiContext)
  {
    GuiContext->ApplyUiScale(uiScale_);
  }
#ifdef __ANDROID__
  if (touchBridge_)
  {
    touchBridge_->SetUiScale(uiScale_);
  }
#endif
}

void UApplication::RenderFrame(int width, int height, double viewDuration)
{
  const auto notifyViewport = [&](UGuiScreenBase *screen)
  {
    if (screen)
    {
      screen->SetViewportInsets(viewportInsetLeft_, viewportInsetTop_,
                                viewportInsetRight_, viewportInsetBottom_);
      screen->OnViewportChanged(width, height);
    }
  };

  if (width > 0 && height > 0)
  {
    glViewport(0, 0, width, height);
  }
  if (State == AppState::MainMenu)
  {
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    GuiContext->Render(width, height, viewportInsetLeft_, viewportInsetTop_,
                       viewportInsetRight_, viewportInsetBottom_);
    return;
  }

  if (State == AppState::InGame && Geometry && World)
  {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    if (auto camera = World->GetCurrentUserCamera())
    {
      const float aspect = static_cast<float>(width) /
                           static_cast<float>(height > 0 ? height : 1);
      camera->SetAspectRatio(aspect);
    }
    Geometry->PrepareFrameRendering();
    const glm::vec4 clearColor = Geometry->GetSkyColor();
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    Geometry->Paint(width, height, viewDuration);
  }

  if (State == AppState::InGame && IconSource)
  {
    IconSource->WarmupPrefabIcons(2);
    IconSource->WarmupCreatureIcons(2);
  }

  if (HudScreen && HudScreen->GetRoot())
  {
    HudScreen->SyncSlotIcons();
    notifyViewport(HudScreen.get());
    GuiContext->RenderOverlay(*HudScreen->GetRoot(), width, height, false);
  }
  if (ConsoleOpen && ConsoleScreen && ConsoleScreen->GetRoot())
  {
    notifyViewport(ConsoleScreen.get());
    GuiContext->RenderOverlay(*ConsoleScreen->GetRoot(), width, height, false);
  }
  if (OverlayPopup && OverlayPopup->IsOpen())
  {
    auto &renderer = GuiContext->GetRenderer();
    renderer.BeginFrame(width, height);
    OverlayPopup->Draw(renderer);
    renderer.EndFrame();
  }
  if (PaletteOpen && PaletteScreen && PaletteScreen->GetRoot())
  {
    notifyViewport(PaletteScreen.get());
    GuiContext->RenderOverlay(*PaletteScreen->GetRoot(), width, height, false);
  }
  if (State == AppState::InGame)
  {
    DrawDragGhost(width, height);
  }
}

bool UApplication::WantsCaptureMouse() const
{
  if (State == AppState::MainMenu)
  {
    return true;
  }
  if (BlocksGameMouseLook())
  {
    return true;
  }
  return GuiContext->WantsCaptureMouse();
}

bool UApplication::WantsCaptureKeyboard() const
{
  if (State == AppState::MainMenu)
  {
    return true;
  }
  return ConsoleOpen || PaletteOpen || GuiContext->WantsCaptureKeyboard();
}

bool UApplication::AllowsWorldMousePlacement() const
{
  return State == AppState::InGame && !PaletteOpen && !ConsoleOpen;
}

bool UApplication::RouteKey(int key, int action, int mods)
{
  GuiKeyEvent event;
  event.keyCode = key;
  event.action = action == GLFW_REPEAT
                     ? GuiKeyAction::Repeat
                     : (action == GLFW_PRESS ? GuiKeyAction::Press
                                             : GuiKeyAction::Release);
  event.mods = mods;

  if (action == GLFW_PRESS && State == AppState::InGame)
  {
    if (key == GLFW_KEY_ESCAPE)
    {
      if (ConsoleOpen && ConsoleScreen && ConsoleScreen->IsPopupOpen())
      {
        if (OverlayPopup)
        {
          OverlayPopup->Close();
        }
        return true;
      }
      if (ConsoleOpen)
      {
        ConsoleOpen = false;
        SuppressConsoleToggleChar = false;
        if (ConsoleScreen)
        {
          ConsoleScreen->SetVisible(false);
        }
        ClearGameplayKeyboard();
        return true;
      }
      if (PaletteOpen)
      {
        PaletteOpen = false;
        if (PaletteScreen)
        {
          PaletteScreen->SetVisible(false);
        }
        return true;
      }
      ReturnToMainMenu();
      return true;
    }
    if (!ConsoleOpen && key == GLFW_KEY_LEFT_ALT)
    {
#ifndef __ANDROID__
      FreeCursor = !FreeCursor;
      if (FreeCursor)
      {
        if (auto *wm = GetWindowManager(Window))
        {
          wm->ResetGameplayMouseCapture();
        }
        if (World && Window)
        {
          double x = 0.0;
          double y = 0.0;
          glfwGetCursorPos(Window, &x, &y);
          if (auto camera = World->GetCurrentUserCamera())
          {
            camera->ResetMouseMove(x, y);
          }
        }
      }
      else
      {
        RecaptureMouseForLook();
      }
      SyncCursorVisibility();
#endif
      return true;
    }
    if (KeyNameIs(Ui.consoleKey, key))
    {
      ConsoleOpen = !ConsoleOpen;
      if (ConsoleScreen)
      {
        ConsoleScreen->SetVisible(ConsoleOpen);
      }
      if (ConsoleOpen)
      {
        ClearGameplayKeyboard();
        SuppressConsoleToggleChar = true;
      }
      else
      {
        SuppressConsoleToggleChar = false;
      }
      SyncCursorVisibility();
      return true;
    }
    if (!ConsoleOpen && key == GLFW_KEY_F5 && World)
    {
      if (auto cam = World->GetCurrentUserCamera())
      {
        cam->CyclePerspective();
        if (Geometry)
        {
          Geometry->ShowTransientMessage(
              CameraPerspectiveLabel(cam->GetPerspective()), 1.5);
        }
      }
      return true;
    }
    if (!ConsoleOpen && KeyNameIs(Ui.paletteKey, key))
    {
      PaletteOpen = !PaletteOpen;
      if (PaletteScreen)
      {
        PaletteScreen->SetVisible(PaletteOpen);
      }
      SyncCursorVisibility();
      return true;
    }
    if (!ConsoleOpen && KeyNameIs(Ui.inventoryKey, key))
    {
      PaletteOpen = !PaletteOpen;
      if (PaletteScreen)
      {
        PaletteScreen->SetVisible(PaletteOpen);
      }
      SyncCursorVisibility();
      return true;
    }
    if (ConsoleOpen && key == GLFW_KEY_ENTER && ConsoleScreen)
    {
      ConsoleScreen->SubmitCommand();
      return true;
    }
    if (!ConsoleOpen)
    {
      const int hotbarSlot = PrimaryHotbarIndexFromGlfwKey(key);
      if (hotbarSlot >= 0 && GameSession)
      {
        if ((mods & GLFW_MOD_ALT) != 0)
        {
          return true;
        }
        GameSession->OnPrimaryHotbarKey(hotbarSlot);
        return true;
      }
    }
  }

  if (action == GLFW_PRESS && State == AppState::MainMenu &&
      key == GLFW_KEY_ESCAPE)
  {
    if (MainMenuScreen && MainMenuScreen->IsQuitConfirmationVisible())
    {
      MainMenuScreen->ShowQuitConfirmation(false);
      return true;
    }
    if (MenuSubview != MenuSubview::Main)
    {
      ShowMainMenu();
      return true;
    }
    if (HasWorldSession() && GameSession)
    {
      GameSession->ResumeGame();
      return true;
    }
    if (MainMenuScreen)
    {
      MainMenuScreen->ShowQuitConfirmation(true);
      return true;
    }
  }

  if (State == AppState::InGame && ConsoleOpen && ConsoleScreen)
  {
    if (KeyNameIs(Ui.consoleKey, key))
    {
      return true;
    }
    ConsoleScreen->RouteKey(event);
    return true;
  }

  if (GuiContext->RouteKey(event))
  {
    return true;
  }
  return false;
}

bool UApplication::RouteChar(unsigned int codepoint)
{
  if (State == AppState::InGame && ConsoleOpen && ConsoleScreen)
  {
    if (SuppressConsoleToggleChar)
    {
      SuppressConsoleToggleChar = false;
      return true;
    }
    ConsoleScreen->RouteChar(GuiCharEvent{codepoint});
    return true;
  }
  SuppressConsoleToggleChar = false;
  return GuiContext->RouteChar(GuiCharEvent{codepoint});
}

bool UApplication::RouteMouseButton(int button, bool pressed, int x, int y,
                                    int pointerId)
{
  GuiMouseEvent event;
  event.x = x;
  event.y = y;
  event.pointerId = pointerId;
  event.button = button == GLFW_MOUSE_BUTTON_RIGHT    ? GuiMouseButton::Right
                 : button == GLFW_MOUSE_BUTTON_MIDDLE ? GuiMouseButton::Middle
                                                      : GuiMouseButton::Left;
  event.pressed = pressed;

  if (State == AppState::InGame)
  {
    DragCursorX = x;
    DragCursorY = y;
    if (event.button == GuiMouseButton::Left && !pressed && GameSession &&
        GameSession->IsDragging())
    {
      SlotAddress target;
      const bool hasTarget = ResolveSlotAt(x, y, target);
      if (hasTarget)
      {
        if (!GameSession->DropOnSlot(target))
        {
          GameSession->CancelDrag();
        }
      }
      else
      {
        GameSession->CancelDrag();
      }
      if (HasAnyOverlayCapture())
      {
        TryRouteInGameOverlay(event, false);
      }
      return true;
    }
    if ((OverlayPopup && OverlayPopup->IsOpen()) || ConsoleOpen)
    {
      if (ConsoleScreen &&
          ConsoleScreen->RouteMouseButton(event, GuiContext->GetRenderer()))
      {
        return true;
      }
    }
    if (event.button == GuiMouseButton::Left)
    {
      if (TryRouteInGameOverlay(event, pressed))
      {
        return true;
      }
      if (FreeCursor && pressed)
      {
        RecaptureMouseForLook();
        return true;
      }
    }
    return false;
  }

  return pressed ? GuiContext->RouteMouseDown(event)
                 : GuiContext->RouteMouseUp(event);
}

bool UApplication::RouteMouseMove(int x, int y, int pointerId)
{
  GuiMouseEvent event;
  event.x = x;
  event.y = y;
  event.pointerId = pointerId;
  if (State == AppState::InGame && HudScreen)
  {
    HudScreen->SetPointerPosition(x, y);
  }
  if (State == AppState::InGame)
  {
    DragCursorX = x;
    DragCursorY = y;
    if (GameSession && GameSession->IsDragging())
    {
      return true;
    }
    if (OverlayPopup && OverlayPopup->IsOpen())
    {
      if (OverlayPopup->OnMouseMove(event))
      {
        return true;
      }
    }
    if (ConsoleOpen && ConsoleScreen &&
        ConsoleScreen->RouteMouseMove(event, GuiContext->GetRenderer()))
    {
      return true;
    }
#if defined(__ANDROID__)
    if (HudScreen && HudScreen->RouteTouchMove(pointerId, x, y))
    {
      return true;
    }
#endif
    auto routeMove = [&](UGuiWidget *root) -> bool
    { return root && root->OnMouseMove(event); };
    bool handled = false;
    if (PaletteOpen)
    {
      handled |= routeMove(PaletteScreen->GetRoot());
    }
    handled |= routeMove(HudScreen ? HudScreen->GetRoot() : nullptr);
    return handled;
  }
  return GuiContext->RouteMouseMove(event);
}

#if defined(__ANDROID__)
void UApplication::ReleaseHudJoystickCapture()
{
  if (State == AppState::InGame && HudScreen)
  {
    HudScreen->ReleaseJoystickCapture();
  }
}

void UApplication::TryToggleFlightOnJumpPress()
{
  if (State != AppState::InGame || !World)
  {
    return;
  }
  auto camera = World->GetCurrentUserCamera();
  if (!camera || !camera->TryToggleFlightOnDoubleSpace())
  {
    return;
  }
  if (Geometry)
  {
    const std::string msg =
        camera->GetFreeMove() ? "Flight ON (Space up, Shift down, 2xSpace off)"
                              : "Flight mode OFF";
    Geometry->ShowTransientMessage(msg, 2.5);
  }
}
#endif

bool UApplication::RouteScroll(double xoffset, double yoffset, int mouseX,
                               int mouseY)
{
  if (State == AppState::InGame)
  {
    GuiScrollEvent event{xoffset, yoffset};
    auto routeScroll = [&](UGuiWidget *root) -> bool
    { return root && root->ScrollAtPoint(mouseX, mouseY, event); };
    if (PaletteOpen &&
        routeScroll(PaletteScreen ? PaletteScreen->GetRoot() : nullptr))
    {
      return true;
    }
    if (ConsoleOpen &&
        routeScroll(ConsoleScreen ? ConsoleScreen->GetRoot() : nullptr))
    {
      return true;
    }
    if (routeScroll(HudScreen ? HudScreen->GetRoot() : nullptr))
    {
      return true;
    }
    return false;
  }
  return GuiContext->RouteScroll(GuiScrollEvent{xoffset, yoffset}, mouseX,
                                 mouseY);
}

} // namespace cutum
