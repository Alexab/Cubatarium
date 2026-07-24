#include "App/Application.h"
#include "Game/Inventory/HotbarInput.h"
#include "Game/Inventory/SlotInteraction.h"

#include "App/Platform/Log.h"
#include "App/Core.h"
#include "App/Platform/CursorCapture.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraPerspective.h"
#ifdef __ANDROID__
#include "App/Platform/TouchInputBridge.h"
#include "Gui/Widgets/GuiTouchControls.h"
#endif
#ifndef __ANDROID__
#include "App/Platform/WindowManager.h"
#endif
#include "Creatures/Player/User.h"
#include "Game/GameSession.h"
#include "Gui/Cache/CreatureIconCache.h"
#include "Gui/Cache/InventoryIconService.h"
#include "Gui/Cache/ObjectIconCache.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiIconSource.h"
#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Gui/Core/GuiMetrics.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiScale.h"
#include "Gui/Core/GuiTypes.h"
#include "Gui/Interfaces/IUGuiClipboard.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Screens/WorldProgressScreen.h"
#include "App/Platform/Log.h"
#include "App/WorldOperationRunner.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Screens/WorldResourcePacksScreen.h"
#include "Gui/Screens/InGameHudScreen.h"
#include "Gui/Screens/LoadWorldScreen.h"
#include "Gui/Screens/MainMenuScreen.h"
#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Screens/SettingsScreen.h"
#include "Gui/Widgets/GuiPopupMenu.h"
#include "Gui/Widgets/GuiWidget.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/TextRenderer.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Textures/TextureCube.h"
#include "World/Core/World.h"
#include "World/Core/WorldLoadDiagnostics.h"
#include "World/Objects/ObjectLibrary.h"

#ifndef __ANDROID__
#include <GLFW/glfw3.h>
#else
#include "App/Platform/GlfwKeyCompat.h"
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
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

class UGlfwClipboard : public IUGuiClipboard
{
public:
  explicit UGlfwClipboard(GLFWwindow *window) : Window(window) {}

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
class UNullClipboard : public IUGuiClipboard
{
public:
  std::string GetString() const override { return {}; }
  void SetString(const std::string &) override {}
};
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
      BlockDefinitions(std::move(block_definitions)), ScreenNav(this)
{
  GuiContext = std::make_unique<UGuiContext>();
  GameSession = std::make_unique<UGameSession>(this, World);
  if (World)
  {
    World->SetOnBlockRegistryChanged([this]() { RefreshBlockCatalog(); });
    World->SetOnCreatureCatalogChanged([this]()
                                       {
                                         RefreshCreatureCatalog();
                                         if (IconSource)
                                         {
                                           IconSource->ClearCreatureIconCache();
                                         }
                                         if (CreaturePreviewRenderer)
                                         {
                                           CreaturePreviewRenderer->Invalidate();
                                         }
                                         if (PaletteScreen)
                                         {
                                           PaletteScreen->InvalidateGrid();
                                         }
                                       });
  }
}

UApplication::~UApplication()
{
  PrepareForShutdown();
}

void UApplication::PrepareForShutdown()
{
  CubatariumSetSuppressErrorDialogs(true);
  WorldOpRunner.reset();
  if (World)
  {
    World->PrepareForShutdown();
    World->SetOnBlockRegistryChanged({});
    World->SetOnCreatureCatalogChanged({});
  }
}

void UApplication::Startup(const std::string &configPath)
{
  StartupOk = false;
  if (Core)
  {
    Core->LoadConfig(configPath);
    Ui = Core->GetUiSettings();
    if (auto defs = Core->GetBlockDefinitionStorage())
    {
      BlockDefinitions = defs;
    }
  }
  if (Geometry)
  {
    Geometry->SetShowHud(Ui.LegacyHud);
    Geometry->SetShowPerformance(Ui.ShowPerformance);
  }
  if (!GuiContext->Initialize(ShaderManager, TextRenderer))
  {
    CubatariumLogError("App", "GuiContext init failed (shaders/GUI)");
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
    float content_scale_x = 1.f;
    float content_scale_y = 1.f;
    glfwGetWindowContentScale(Window, &content_scale_x, &content_scale_y);
    PlatformUiMetrics platform;
    platform.ContentScaleX = content_scale_x;
    platform.ContentScaleY = content_scale_y;
    UpdateUiScale(fbW > 0 ? fbW : 1280, fbH > 0 ? fbH : 720, platform);
#endif
  }
  if (Core && BlockDefinitions)
  {
    const std::string typesPath = "content/types.json";
    if (!UWorldGenRefs::LoadFromFile("content/worldgen_refs.json"))
    {
      CubatariumLogInfo("App", "worldgen_refs.json not loaded — using legacy block name resolution");
    }
    if (!UObjectFeatureConfigStorage::LoadFromFile("content/object_features.json"))
    {
      CubatariumLogInfo("App", "object_features.json not loaded — legacy tree placement only");
    }
    if (!UWorldGenPack::LoadPackId("default"))
    {
      CubatariumLogInfo("App", "worldgen pack not loaded — built-in biome defaults only");
    }
    GameSession->InitializeCatalog(typesPath, *Core);
  }
  GameSession->RegisterCommands();

  if (Core && BlockDefinitions && ShaderManager)
  {
    auto textures = Core->GetTextureCubeStorage();
    std::shared_ptr<UCreaturePreviewRenderer> creaturePreview;
    if (World)
    {
      creaturePreview = std::make_shared<UCreaturePreviewRenderer>(
          World->GetCreatureDefinitionStorage(),
          World->GetSkinDefinitionStorage(), Core->GetCreatureTextureStorage(),
          ShaderManager);
      if (!creaturePreview->Initialize())
      {
        creaturePreview.reset();
      }
    }
    CreaturePreviewRenderer = creaturePreview;

    auto iconService = std::make_shared<UInventoryIconService>();
    if (!iconService->Initialize())
    {
      iconService.reset();
    }
    auto objectCache = std::make_unique<UObjectIconCache>(
        Core->GetObjectLibrary(), textures, BlockDefinitions, ShaderManager,
        iconService);
    if (objectCache->Initialize())
    {
      std::unique_ptr<UCreatureIconCache> creatureCache;
      if (creaturePreview)
      {
        creatureCache =
            std::make_unique<UCreatureIconCache>(creaturePreview, iconService);
        if (!creatureCache->Initialize())
        {
          creatureCache.reset();
        }
      }
      IconSource = std::make_unique<UGuiIconSource>(
          textures, std::move(objectCache), std::move(creatureCache));
    }
    auto previewRenderer = std::make_unique<UContentPreviewRenderer>(
        Core->GetObjectLibrary(), textures, BlockDefinitions, ShaderManager,
        creaturePreview);
    if (previewRenderer->Initialize())
    {
      ContentPreviewRenderer = std::move(previewRenderer);
    }
  }

  State = AppState::MainMenu;
  ShowMainMenu();
  StartupOk = true;
}

void UApplication::ScheduleEnterGame() { PendingEnterGame = true; }

void UApplication::ScheduleQuit()
{
  PendingQuit = true;
  PendingShutdownSave = HasWorldSession();
}

void UApplication::BeginShutdownOperation(const bool saveSession,
                                        const bool closeAfter)
{
  if (!Core || !World || State == AppState::Loading)
  {
    RequestQuit();
    return;
  }
  ShutdownCloseAfter = closeAfter;
  WorldRunnerRequest request;
  request.op = WorldRunnerOp::Shutdown;
  request.shutdownSaveSession = saveSession;
  request.shutdownCloseApplication = closeAfter;
  request.saveConfigAfter = false;
  BeginWorldOperation(std::move(request),
                      [this]()
                      {
                        if (GameSession)
                        {
                          GameSession->SaveCommandHistory();
                        }
                          if (Core)
                          {
                            // World terrain already saved by ShutdownSave.
                            // SaveSystem→SaveWorld would re-enter snapshot/
                            // InitChunkScheduler and can hang on worker join.
                            Core->SaveConfigFile();
                          }
                        if (ShutdownCloseAfter)
                        {
                          RequestQuit();
                        }
                      });
}

bool UApplication::TryBeginShutdownFromWindowClose()
{
  if (!Core || !World || State == AppState::Loading)
  {
    return false;
  }
  BeginShutdownOperation(HasWorldSession(), true);
  return true;
}

void UApplication::RequestEnterGame()
{
  if (State == AppState::InGame)
  {
    return;
  }

  if (!WorldSessionActive)
  {
    WorldRunnerRequest request;
    request.op = WorldRunnerOp::EnterGame;
    request.enterGameAfter = true;
    BeginWorldOperation(std::move(request));
    return;
  }

  GuiContext->SetScreen(nullptr);
  State = AppState::InGame;
  EnterInGameInputState();
}

void UApplication::ShowWorldProgressScreen()
{
  ConsoleOpen = false;
  PaletteOpen = false;
  FreeCursor = true;
  auto screen = std::make_unique<UWorldProgressScreen>();
  ProgressScreen = screen.get();
  GuiContext->SetScreen(std::move(screen));
  SyncCursorVisibility();
}

void UApplication::BeginWorldOperation(WorldRunnerRequest request,
                                     std::function<void()> onComplete)
{
  if (!Core || !World)
  {
    return;
  }
  WorldOpOnComplete = std::move(onComplete);
  if (!WorldOpRunner)
  {
    WorldOpRunner = std::make_unique<UWorldOperationRunner>(*Core, *World);
  }
  State = AppState::Loading;
  ShowWorldProgressScreen();
  ProgressSink.Begin(WorldOperationKind::Load);
  ProgressSink.Report("init", 0.f, "Starting...");
  if (ProgressScreen)
  {
    ProgressScreen->ApplySnapshot(ProgressSink.Get());
  }
  WorldOpRunner->Start(std::move(request));
}

void UApplication::OnWorldOperationFinished()
{
  const bool success = WorldOpRunner && WorldOpRunner->Succeeded();
  if (WorldOpRunner && WorldOpRunner->IsShutdownOperation())
  {
    if (success && WorldOpOnComplete)
    {
      auto callback = std::move(WorldOpOnComplete);
      WorldOpOnComplete = nullptr;
      callback();
    }
    else if (!success && WorldOpRunner->ShouldCloseApplication())
    {
      RequestQuit();
    }
    return;
  }
  if (!success)
  {
    ShowMainMenu();
    State = AppState::MainMenu;
    WorldOpOnComplete = nullptr;
    return;
  }

  if (WorldOpRunner && WorldOpRunner->ShouldEnterGame())
  {
    EnterGameAfterWorldChange();
  }
  else if (WorldOpOnComplete)
  {
    auto callback = std::move(WorldOpOnComplete);
    WorldOpOnComplete = nullptr;
    callback();
  }
  else
  {
    ShowMainMenu();
    State = AppState::MainMenu;
  }
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
  ScreenNav.ShowMainMenu();
}

void UApplication::SetHotbarCountSetting(int count)
{
  Ui.HotbarCount = std::clamp(count, 1, 2);
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
      World->EnsurePlayerHotbarCount(user, static_cast<size_t>(Ui.HotbarCount));
    }
  }
}

void UApplication::ReturnToMainMenu()
{
  ScreenNav.ReturnToMainMenu();
}

void UApplication::ShowSettings()
{
  ScreenNav.ShowSettings();
}

void UApplication::ShowWorldSettings()
{
  ScreenNav.ShowWorldSettings();
}

void UApplication::ShowNewWorld()
{
  ScreenNav.ShowNewWorld();
}

void UApplication::ShowLoadWorld()
{
  ScreenNav.ShowLoadWorld();
}

void UApplication::SaveWorldSessionIfNeeded()
{
  SaveActiveWorldIfNeeded();
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

void UApplication::RefreshBlockCatalog()
{
  if (!GameSession || !Core)
  {
    return;
  }
  if (auto defs = Core->GetBlockDefinitionStorage())
  {
    BlockDefinitions = defs;
  }
  if (!BlockDefinitions)
  {
    return;
  }
  GameSession->ReindexBlockCatalog(*Core);
  if (IconSource)
  {
    IconSource->ClearBlockIconCache();
  }
  if (PaletteScreen)
  {
    PaletteScreen->InvalidateGrid();
  }
}

void UApplication::RefreshCreatureCatalog()
{
  if (!GameSession || !Core)
  {
    return;
  }
  if (auto defs = Core->GetBlockDefinitionStorage())
  {
    BlockDefinitions = defs;
  }
  if (!BlockDefinitions)
  {
    return;
  }
  GameSession->ReindexBlockCatalog(*Core);
}

void UApplication::EnterGameAfterWorldChange()
{
  WorldSessionActive = true;
  if (Core && World)
  {
    Ui = Core->GetUiSettings();
    if (auto user = World->GetCurrentUser())
    {
      World->EnsurePlayerHotbarCount(user, static_cast<size_t>(Ui.HotbarCount));
    }
    if (Geometry)
    {
      Geometry->SetShowHud(Ui.LegacyHud);
    }
    World->PrepareEnterGameSession();
    LogWorldLoadDiag("enter_game_after_world_change", *World);
  }
  RefreshBlockCatalog();
  ShowInGameHud();
  State = AppState::InGame;
  EnterInGameInputState();
}

void UApplication::ScheduleDeferredMenuAction(std::function<void()> Action)
{
  PendingMenuAction = std::move(Action);
}

void UApplication::SaveIfNeededAndProceed(std::function<void()> proceed)
{
  if (!proceed)
  {
    return;
  }
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
  UpdateUiScale(LastFramebufferWidth, LastFramebufferHeight, LastPlatformMetrics);
  if (Geometry)
  {
    Geometry->SetShowHud(Ui.LegacyHud);
    Geometry->SetShowPerformance(Ui.ShowPerformance);
  }
  if (World)
  {
    if (auto user = World->GetCurrentUser())
    {
      World->EnsurePlayerHotbarCount(user, static_cast<size_t>(Ui.HotbarCount));
    }
  }
  SyncCursorVisibility();
}

void UApplication::CreateNewWorldWithSettings(
    const ProceduralSettings &settings,
    const std::vector<std::string> &resourcePacksEnabled)
{
  ResourcePackSelection selection;
  selection.Primary = resourcePacksEnabled;
  if (!selection.Primary.empty())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }
  CreateNewWorldWithSettings(settings, selection);
}

void UApplication::CreateNewWorldWithSettings(
    const ProceduralSettings &settings, const ResourcePackSelection &selection)
{
  CreateNewWorldWithSettings(settings, selection, WorldViewSettings{});
}

void UApplication::CreateNewWorldWithSettings(
    const ProceduralSettings &settings, const ResourcePackSelection &selection,
    const WorldViewSettings &view)
{
  if (!Core)
  {
    return;
  }
  WorldRunnerRequest request;
  request.op = WorldSessionActive && World && !World->GetWorldName().empty()
                   ? WorldRunnerOp::SaveThenCreate
                   : WorldRunnerOp::Create;
  request.settings = settings;
  request.packs = selection;
  request.view = view;
  request.enterGameAfter = true;
  request.saveConfigAfter = true;
  BeginWorldOperation(std::move(request));
}

std::vector<InstalledPackInfo> UApplication::ListInstalledResourcePacks() const
{
  return Core ? Core->ListInstalledResourcePacks()
              : std::vector<InstalledPackInfo>{};
}

std::vector<std::string> UApplication::GetDefaultEnabledResourcePacks() const
{
  return Core ? Core->GetDefaultEnabledResourcePacks()
              : std::vector<std::string>{};
}

ResourcePackSelection UApplication::GetDefaultResourcePackSelection() const
{
  return Core ? Core->GetDefaultResourcePackSelection()
              : ResourcePackSelection{};
}

std::vector<std::string>
UApplication::PeekWorldResourcePacks(const std::string &worldName) const
{
  return Core ? Core->PeekWorldResourcePacks(worldName)
              : std::vector<std::string>{};
}

ResourcePackSelection
UApplication::GetCurrentWorldResourcePackSelection() const
{
  return Core ? Core->GetCurrentWorldResourcePackSelection()
              : ResourcePackSelection{};
}

bool UApplication::ApplyResourcePacksToCurrentWorld(
    const ResourcePackSelection &selection)
{
  if (!Core)
  {
    return false;
  }
  if (!Core->ApplyResourcePacksToCurrentWorld(selection))
  {
    return false;
  }
  RefreshBlockCatalog();
  return true;
}

WorldViewSettings UApplication::GetCurrentWorldViewSettings() const
{
  return Core ? Core->GetCurrentWorldViewSettings() : WorldViewSettings{};
}

bool UApplication::ApplyViewSettingsToCurrentWorld(const WorldViewSettings &view)
{
  if (!Core)
  {
    return false;
  }
  return Core->ApplyViewSettingsToCurrentWorld(view);
}

void UApplication::LoadSelectedWorld(const std::string &worldName)
{
  if (!Core)
  {
    return;
  }
  WorldRunnerRequest request;
  request.op = WorldSessionActive && World && !World->GetWorldName().empty()
                   ? WorldRunnerOp::SaveThenLoad
                   : WorldRunnerOp::Load;
  request.worldName = worldName;
  request.enterGameAfter = true;
  request.saveConfigAfter = true;
  BeginWorldOperation(std::move(request));
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
  ProgressScreen = nullptr;
#ifndef __ANDROID__
  if (Window && !Clipboard)
  {
    Clipboard = std::make_unique<UGlfwClipboard>(Window);
    GuiContext->SetClipboard(Clipboard.get());
  }
#else
  if (!Clipboard)
  {
    Clipboard = std::make_unique<UNullClipboard>();
    GuiContext->SetClipboard(Clipboard.get());
  }
#endif

  GameSession->InitCommandHistory(GetExecutableDirectory() /
                                   "console_history.txt");

  IUGuiIconSource *icons = IconSource.get();
  UContentPreviewRenderer *preview = ContentPreviewRenderer.get();
  auto hud = std::make_unique<UInGameHudScreen>(
      GameSession.get(), &GuiContext->GetTheme(), icons);
#if defined(__ANDROID__)
  if (TouchBridge)
  {
    hud->ConfigureTouchControls(
        TouchBridge, [this]() { ReturnToMainMenu(); },
        [this]()
        {
          PaletteOpen = !PaletteOpen;
#if defined(__ANDROID__)
          if (PaletteOpen && HudScreen)
          {
            HudScreen->ReleaseTouchCaptures();
          }
#endif
          if (PaletteScreen)
          {
            PaletteScreen->SetVisible(PaletteOpen);
          }
          SyncCursorVisibility();
        },
        [this]()
        {
          ConsoleOpen = !ConsoleOpen;
#if defined(__ANDROID__)
          if (ConsoleOpen && HudScreen)
          {
            HudScreen->ReleaseTouchCaptures();
          }
#endif
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
        [this]() { TryToggleFlightOnJumpPress(); },
        TouchIsoControlCallbacks{
            [this]() -> bool
            {
              if (!World)
              {
                return false;
              }
              if (auto cam = World->GetCurrentUserCamera())
              {
                return cam->IsIsometricProjection();
              }
              return false;
            },
            [this](int steps)
            {
              if (!World)
              {
                return;
              }
              if (auto cam = World->GetCurrentUserCamera())
              {
                cam->SnapIsoCameraYaw(steps);
              }
            },
            [this](float scroll)
            {
              if (!World)
              {
                return;
              }
              if (auto cam = World->GetCurrentUserCamera())
              {
                cam->UpdateMouseScroll(0.0, static_cast<double>(scroll));
              }
            },
            [this]()
            {
              if (!World)
              {
                return;
              }
              if (auto cam = World->GetCurrentUserCamera())
              {
                cam->CyclePerspective();
                if (Geometry)
                {
                  Geometry->ShowTransientMessage(
                      cam->GetViewController().ViewLabel(*cam), 1.5);
                }
                if (HudScreen)
                {
                  HudScreen->InvalidateTouchControlsLayout();
                }
              }
            }});
  }
#endif
  hud->OnAttach(*GuiContext);
  hud->Build(*GuiContext);
  HudScreen = std::move(hud);

  OverlayPopup = std::make_unique<UGuiPopupMenu>(&GuiContext->GetTheme());

  ConsoleScreen = std::make_unique<UConsoleScreen>(GameSession.get());
  ConsoleScreen->OnAttach(*GuiContext);
  ConsoleScreen->Build(*GuiContext);
  ConsoleScreen->AttachPopup(OverlayPopup.get());
  ConsoleScreen->SetVisible(false);

  PaletteScreen = std::make_unique<UCreativePaletteScreen>(
      &GameSession->GetContentCatalog(), GameSession.get(), icons, preview);
  PaletteScreen->OnAttach(*GuiContext);
  PaletteScreen->Build(*GuiContext);
  PaletteScreen->SetVisible(false);

  WorldGenScreen = std::make_unique<UWorldGenPaletteScreen>(
      World.get(), &GameSession->GetContentCatalog(), icons, preview);
  WorldGenScreen->OnAttach(*GuiContext);
  WorldGenScreen->Build(*GuiContext);
  WorldGenScreen->SetVisible(false);

  GuiContext->SetScreen(nullptr);
}

bool UApplication::UsesUiPointer() const
{
  return State == AppState::MainMenu || State == AppState::Loading || FreeCursor ||
         ConsoleOpen || PaletteOpen || WorldGenOpen;
}

bool UApplication::BlocksGameMouseLook() const
{
  return State == AppState::InGame &&
         (FreeCursor || ConsoleOpen || PaletteOpen || WorldGenOpen);
}

AppCursorPolicy UApplication::GetCursorPolicy() const
{
  if (UsesUiPointer())
  {
    return AppCursorPolicy::Free;
  }
  if (State == AppState::InGame)
  {
    if (Ui.ControlScheme == ControlScheme::Classic)
    {
      return AppCursorPolicy::CapturedHidden;
    }
    return AppCursorPolicy::ConfinedVisible;
  }
  return AppCursorPolicy::Free;
}

void UApplication::SyncGameplayLookCapture()
{
  if (!World)
  {
    return;
  }
#ifndef __ANDROID__
  if (Window)
  {
    if (auto *wm = GetWindowManager(Window))
    {
      wm->ResetGameplayMouseCapture();
      return;
    }
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(Window, &x, &y);
    if (auto camera = World->GetCurrentUserCamera())
    {
      camera->ResetMouseMove(x, y);
    }
  }
#else
  if (TouchBridge)
  {
    TouchBridge->ConsumeMouseDelta();
    const glm::vec2 pos = TouchBridge->GetMousePosition();
    if (auto camera = World->GetCurrentUserCamera())
    {
      camera->ResetMouseMove(static_cast<double>(pos.x),
                             static_cast<double>(pos.y));
    }
  }
#endif
}

void UApplication::SyncCursorVisibility()
{
  const AppCursorPolicy policy = GetCursorPolicy();
  const bool leavingUiPointer = LastCursorPolicy == AppCursorPolicy::Free &&
                                policy != AppCursorPolicy::Free &&
                                State == AppState::InGame;

#ifndef __ANDROID__
  if (Window)
  {
    ApplyCursorPolicy(Window, policy);
  }
#endif

  if (leavingUiPointer)
  {
    SyncGameplayLookCapture();
  }

  LastCursorPolicy = policy;
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
#if defined(__ANDROID__)
  if (TouchBridge)
  {
    TouchBridge->ResetSprint();
  }
#endif
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
  WorldGenOpen = false;
  FreeCursor = false;
  if (WorldGenScreen)
  {
    WorldGenScreen->SetVisible(false);
  }
  if (PaletteScreen)
  {
    PaletteScreen->SetVisible(false);
  }
#if defined(__ANDROID__)
  Ui.ControlScheme = ControlScheme::Cubatarium;
#endif
  GuiContext->ClearInputState();
  SyncCursorVisibility();
}

void UApplication::RecaptureMouseForLook()
{
  FreeCursor = false;
  EnterInGameInputState();
}

int UApplication::NormalizeOverlayPointer(int PointerId) const
{
  if (PointerId < 0)
  {
    return 0;
  }
  if (PointerId >= kMaxOverlayPointers)
  {
    return kMaxOverlayPointers - 1;
  }
  return PointerId;
}

bool UApplication::HasAnyOverlayCapture() const
{
  for (const OverlayPointerCapture capture : OverlayCaptures)
  {
    if (capture != OverlayPointerCapture::None)
    {
      return true;
    }
  }
  return false;
}

bool UApplication::TryRouteInGameOverlay(const GuiMouseEvent &event,
                                         bool Pressed)
{
  const int pointerIndex = NormalizeOverlayPointer(event.PointerId);

  auto routeRoot = [&](UGuiWidget *root, bool requireHitTest) -> bool
  {
    if (!root)
    {
      return false;
    }
    if (requireHitTest && !root->HitTest(event.X, event.Y))
    {
      return false;
    }
    return Pressed ? root->OnMouseDown(event) : root->OnMouseUp(event);
  };

  if (Pressed)
  {
#if defined(__ANDROID__)
    if (HudScreen && HudScreen->HitTestTouchControls(event.X, event.Y))
    {
      if (routeRoot(HudScreen->GetRoot(), true))
      {
        OverlayCaptures[pointerIndex] = OverlayPointerCapture::Hud;
        return true;
      }
    }
#endif
    if (WorldGenOpen && routeRoot(WorldGenScreen->GetRoot(), true))
    {
      OverlayCaptures[pointerIndex] = OverlayPointerCapture::WorldGen;
      return true;
    }
    if (PaletteOpen && routeRoot(PaletteScreen->GetRoot(), true))
    {
      OverlayCaptures[pointerIndex] = OverlayPointerCapture::Palette;
      return true;
    }
    if (ConsoleOpen && routeRoot(ConsoleScreen->GetRoot(), true))
    {
      OverlayCaptures[pointerIndex] = OverlayPointerCapture::Console;
      return true;
    }
    if (routeRoot(HudScreen ? HudScreen->GetRoot() : nullptr, true))
    {
      OverlayCaptures[pointerIndex] = OverlayPointerCapture::Hud;
      return true;
    }
    return false;
  }

  const OverlayPointerCapture capture = OverlayCaptures[pointerIndex];
  OverlayCaptures[pointerIndex] = OverlayPointerCapture::None;
  if (capture == OverlayPointerCapture::None)
  {
    return false;
  }

  switch (capture)
  {
  case OverlayPointerCapture::Palette:
    return PaletteOpen && routeRoot(PaletteScreen->GetRoot(), false);
  case OverlayPointerCapture::WorldGen:
    return WorldGenOpen && routeRoot(WorldGenScreen->GetRoot(), false);
  case OverlayPointerCapture::Console:
    return ConsoleOpen && routeRoot(ConsoleScreen->GetRoot(), false);
  case OverlayPointerCapture::Hud:
  {
    const bool handled =
        routeRoot(HudScreen ? HudScreen->GetRoot() : nullptr, false);
#if defined(__ANDROID__)
    ReleaseHudJoystickCaptureForPointer(event.PointerId);
#endif
    return handled;
  }
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
  if (!GameSession || !GameSession->IsDragging() || !IconSource ||
      !GuiContext)
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
    tex = IconSource->GetBlockIconTexture(drag.entry.Id);
    break;
  case InventoryEntryKind::Object:
    tex = IconSource->GetObjectIconTexture(drag.entry.Id);
    break;
  case InventoryEntryKind::UCreature:
    tex = IconSource->GetCreatureIconTexture(drag.entry.Id);
    break;
  case InventoryEntryKind::Skin:
    tex = IconSource->GetSkinIconTexture(drag.entry.Id);
    break;
  }
  if (tex == 0)
  {
    return;
  }
  const int size = GuiContext->GetTheme().HotbarSlotSize;
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
    BeginShutdownOperation(PendingShutdownSave, true);
    return;
  }
  if (PendingMenuAction)
  {
    auto Action = std::move(PendingMenuAction);
    PendingMenuAction = nullptr;
    Action();
  }

  if (State == AppState::Loading)
  {
#if defined(__ANDROID__)
    constexpr int kAndroidLoadChunkBudget = 4;
    constexpr int kLoadChunkBudget = kAndroidLoadChunkBudget;
#else
    constexpr int kLoadChunkBudget = 16;
#endif
    if (WorldOpRunner)
    {
      if (WorldOpRunner->IsEnterGameGpuWarmupStage())
      {
        constexpr int kGpuWarmupMaxFrames = 8;
        constexpr int kGpuWarmupMinFrames = 3;
        constexpr int kGpuWarmupMeshBudget = 16;
        constexpr int kGpuWarmupStreamingBudget = 8;
        const int remaining = WorldOpRunner->EnterGameGpuWarmupFramesRemaining();
        const int frame = kGpuWarmupMaxFrames - remaining;
        if (Geometry && World)
        {
          if (frame == 0)
          {
            Geometry->ResetWorldRenderState();
            LogWorldLoadDiag("gpu_warmup_reset", *World);
          }
          else
          {
            if (World->NeedsEnterGameMeshWarmup())
            {
              World->DrainEnterGameMeshWarmup(kGpuWarmupMeshBudget);
            }
            // Cooperative load already prepared spawn; streaming Tick here races
            // mesh Dirty iterators and was crashing EnterGame (0x80000003).
            if (!World->IsSpawnAreaPreparedByCooperativeLoad())
            {
              World->TickEnterStreamingWarmup(kGpuWarmupStreamingBudget);
            }
          }
          const bool upload_ready =
              frame >= kGpuWarmupMinFrames - 1 &&
              !World->NeedsEnterGameMeshWarmup();
          if (upload_ready)
          {
            World->WarmupVisibleListAtCamera();
            Geometry->WarmupGreedyGpuFromWorld();
            LogWorldLoadDiag("gpu_warmup_draw", *World);
          }
        }
        WorldOpRunner->AdvanceEnterGameGpuWarmup(ProgressSink);
      }
      if (WorldOpRunner->Tick(ProgressSink, kLoadChunkBudget))
      {
        OnWorldOperationFinished();
      }
    }
    if (ProgressScreen)
    {
      ProgressScreen->ApplySnapshot(ProgressSink.Get());
    }
    GuiContext->Update(dt);
    SyncCursorVisibility();
    return;
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
    if (WorldGenScreen)
    {
      WorldGenScreen->Update(dt);
    }
  }
  SyncCursorVisibility();
}

void UApplication::ProcessInput() { (void)0; }

void UApplication::SetViewportInsets(int left, int top, int right, int bottom)
{
  const int newLeft = std::max(0, left);
  const int newTop = std::max(0, top);
  const int newRight = std::max(0, right);
  const int newBottom = std::max(0, bottom);
  constexpr int kInsetHysteresisPx = 16;
  if (newLeft == ViewportInsetLeft && newTop == ViewportInsetTop &&
      newRight == ViewportInsetRight &&
      std::abs(newBottom - ViewportInsetBottom) < kInsetHysteresisPx)
  {
    return;
  }
  ViewportInsetLeft = newLeft;
  ViewportInsetTop = newTop;
  ViewportInsetRight = newRight;
  ViewportInsetBottom = newBottom;
  if (Geometry)
  {
    Geometry->SetOverlayMargins(ViewportInsetLeft + 16, ViewportInsetRight + 16,
                                ViewportInsetTop + 30);
  }
}

void UApplication::SetKeyboardInsetBottom(int bottom)
{
  const int inset = std::max(0, bottom);
  if (KeyboardInsetBottom == inset)
  {
    return;
  }
  KeyboardInsetBottom = inset;
  if (ConsoleScreen)
  {
    ConsoleScreen->SetKeyboardInsetBottom(KeyboardInsetBottom);
  }
}

void UApplication::SetUiScale(float scale)
{
  if (std::fabs(scale - UiScale) < 0.01f)
  {
    return;
  }
  UiScale = scale;
  if (GuiContext)
  {
    GuiContext->ApplyUiScale(UiScale);
    NotifyAllScreensMetricsChanged(GuiContext->GetMetrics());
  }
#ifdef __ANDROID__
  if (TouchBridge)
  {
    TouchBridge->SetUiScale(UiScale);
  }
#endif
}

void UApplication::UpdateUiScale(int fb_w, int fb_h,
                                 const PlatformUiMetrics &platform)
{
  LastPlatformMetrics = platform;
  LastFramebufferWidth = fb_w;
  LastFramebufferHeight = fb_h;
  PlatformUiMetrics metrics = platform;
  metrics.ScreenWidthPx = fb_w;
  metrics.ScreenHeightPx = fb_h;
  const float effective =
      ResolveEffectiveUiScale(Ui.UiScaleUser, metrics);
  SetUiScale(effective);
}

void UApplication::ApplyLiveUiScale(float user_scale)
{
  Ui.UiScaleUser = std::clamp(user_scale, kGuiMinUserScale, kGuiMaxUserScale);
  PlatformUiMetrics metrics = LastPlatformMetrics;
  metrics.ScreenWidthPx = LastFramebufferWidth;
  metrics.ScreenHeightPx = LastFramebufferHeight;
  const float effective =
      ResolveEffectiveUiScale(Ui.UiScaleUser, metrics);
  SetUiScale(effective);
}

void UApplication::NotifyAllScreensMetricsChanged(const GuiMetrics &metrics)
{
  if (HudScreen)
  {
    HudScreen->OnMetricsChanged(metrics);
  }
  if (ConsoleScreen)
  {
    ConsoleScreen->OnMetricsChanged(metrics);
  }
  if (PaletteScreen)
  {
    PaletteScreen->OnMetricsChanged(metrics);
  }
  if (GuiContext && GuiContext->GetScreen())
  {
    GuiContext->GetScreen()->OnMetricsChanged(metrics);
  }
}

void UApplication::RenderFrame(int width, int height, double viewDuration)
{
  const auto notifyViewport = [&](UGuiScreenBase *screen)
  {
    if (screen)
    {
      screen->SetViewportInsets(ViewportInsetLeft, ViewportInsetTop,
                                ViewportInsetRight, ViewportInsetBottom);
      screen->OnViewportChanged(width, height);
    }
  };

  if (width > 0 && height > 0)
  {
    glViewport(0, 0, width, height);
  }
  else
  {
    CubatariumLogError("App", "RenderFrame skipped viewport: zero framebuffer size");
    return;
  }
  if (State == AppState::MainMenu || State == AppState::Loading)
  {
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    GuiContext->Render(width, height, ViewportInsetLeft, ViewportInsetTop,
                        ViewportInsetRight, ViewportInsetBottom);
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
      camera->SetViewportSize(width, height);
    }
    const auto prepare_begin = std::chrono::high_resolution_clock::now();
    Geometry->PrepareFrameRendering();
    const glm::vec4 clearColor = Geometry->GetSkyColor();
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (Geometry->IsGradientSky())
    {
      Geometry->DrawSkyGradient();
    }
    World->SetLastPrepareFrameMs(
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - prepare_begin)
            .count());
    Geometry->Paint(width, height, viewDuration);
  }

  const auto gui_begin = std::chrono::high_resolution_clock::now();
  if (State == AppState::InGame && IconSource)
  {
    IconSource->WarmupObjectIcons(2);
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
    GuiContext->RenderOverlay(*ConsoleScreen->GetRoot(), width, height,
                               false);
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
    PaletteScreen->RenderPreview();
    notifyViewport(PaletteScreen.get());
    GuiContext->RenderOverlay(*PaletteScreen->GetRoot(), width, height, false);
  }
  if (WorldGenOpen && WorldGenScreen && WorldGenScreen->GetRoot())
  {
    WorldGenScreen->RenderPreview();
    notifyViewport(WorldGenScreen.get());
    GuiContext->RenderOverlay(*WorldGenScreen->GetRoot(), width, height, false);
  }
#if defined(__ANDROID__)
  if (HudScreen && (PaletteOpen || WorldGenOpen))
  {
    HudScreen->RenderTouchControlsOverlay(*GuiContext, width, height);
  }
#endif
  if (State == AppState::InGame)
  {
    DrawDragGhost(width, height);
  }
  if (World)
  {
    World->SetLastGuiOverlayMs(
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - gui_begin)
            .count());
  }
}

bool UApplication::WantsCaptureMouse() const
{
  if (State == AppState::MainMenu || State == AppState::Loading)
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
  if (State == AppState::MainMenu || State == AppState::Loading)
  {
    return true;
  }
  return ConsoleOpen || PaletteOpen || WorldGenOpen ||
         GuiContext->WantsCaptureKeyboard();
}

bool UApplication::AllowsWorldMousePlacement() const
{
  return State == AppState::InGame && !ConsoleOpen && !PaletteOpen &&
         !WorldGenOpen;
}

bool UApplication::RouteKey(int key, int Action, int Mods)
{
  return InputRouter.RouteKey(*this, key, Action, Mods);
}

bool UApplication::RouteChar(unsigned int Codepoint)
{
  return InputRouter.RouteChar(*this, Codepoint);
}

bool UApplication::RouteMouseButton(int Button, bool Pressed, int x, int y,
                                    int PointerId)
{
  return InputRouter.RouteMouseButton(*this, Button, Pressed, x, y, PointerId);
}

bool UApplication::RouteMouseMove(int x, int y, int PointerId)
{
  return InputRouter.RouteMouseMove(*this, x, y, PointerId);
}

#if defined(__ANDROID__)
void UApplication::ReleaseHudJoystickCapture()
{
  if (State == AppState::InGame && HudScreen)
  {
    HudScreen->ReleaseJoystickCapture();
  }
}

void UApplication::ReleaseHudJoystickCaptureForPointer(int pointerId)
{
  if (State == AppState::InGame && HudScreen)
  {
    HudScreen->ReleaseJoystickCaptureForPointer(pointerId);
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

void UApplication::SubmitConsoleCommand()
{
  if (State == AppState::InGame && ConsoleOpen && ConsoleScreen)
  {
    ConsoleScreen->SubmitCommand();
  }
}
#endif

bool UApplication::RouteScroll(double Xoffset, double Yoffset, int mouseX,
                               int mouseY)
{
  return InputRouter.RouteScroll(*this, Xoffset, Yoffset, mouseX, mouseY);
}

} // namespace cutum
