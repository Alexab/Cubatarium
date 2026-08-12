#include "App/Application.h"
#include "Game/Inventory/HotbarInput.h"
#include "Game/Inventory/SlotInteraction.h"
#include "World/Diagnostics/EnterLitDiagnostics.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Core/RuntimeTuning.h"

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
#include "Items/FpViewmodelRenderer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Stats/CreatureVitals.h"
#include "Game/ModePolicy.h"
#include "Gui/Cache/ItemIconCache.h"
#include "Gui/Cache/ObjectIconCache.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiIconSource.h"
#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Gui/Preview/ItemPreviewRenderer.h"
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
#include "Gui/Screens/SurvivalInventoryScreen.h"
#include "Gui/Screens/DeathScreen.h"
#include "Gui/Screens/CraftingScreen.h"
#include "Gui/Screens/AnvilScreen.h"
#include "Gui/Screens/CharacterSheetScreen.h"
#include "Gui/Screens/WorldResourcePacksScreen.h"
#include "Gui/Screens/InGameHudScreen.h"
#include "Gui/Screens/LoadWorldScreen.h"
#include "Gui/Screens/MainMenuScreen.h"
#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Screens/SettingsScreen.h"
#include "Gui/Widgets/GuiPopupMenu.h"
#include "Gui/Widgets/GuiSlot.h"
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
#include "World/View/WorldViewSettings.h"

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

    std::shared_ptr<UItemPreviewRenderer> itemPreview;
    if (Core && ShaderManager)
    {
      itemPreview = std::make_shared<UItemPreviewRenderer>(
          Core->GetItemDefinitionStorage(), ShaderManager);
      if (!itemPreview->Initialize())
      {
        itemPreview.reset();
      }
    }

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
          textures, std::move(objectCache), std::move(creatureCache),
          std::make_unique<UItemIconCache>(Core->GetItemDefinitionStorage(),
                                           iconService, itemPreview));
    }
    auto previewRenderer = std::make_unique<UContentPreviewRenderer>(
        Core->GetObjectLibrary(), textures, BlockDefinitions, ShaderManager,
        creaturePreview, itemPreview);
    if (previewRenderer->Initialize())
    {
      ContentPreviewRenderer = std::move(previewRenderer);
    }

    if (Core && ShaderManager)
    {
      auto fpView = std::make_unique<UFpViewmodelRenderer>(
          Core->GetItemDefinitionStorage(), BlockDefinitions,
          Core->GetTextureCubeStorage(), Core->GetCreatureTextureStorage(),
          ShaderManager);
      if (fpView->Initialize())
      {
        FpViewmodelRenderer = std::move(fpView);
      }
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
  if (!Core || !World)
  {
    return false;
  }
  if (State == AppState::Loading)
  {
    RequestQuit();
    return true;
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
  const WorldOperationKind kind = [&]()
  {
    switch (request.op)
    {
    case WorldRunnerOp::Save:
    case WorldRunnerOp::SaveThenLoad:
    case WorldRunnerOp::SaveThenCreate:
      return WorldOperationKind::Save;
    case WorldRunnerOp::Create:
      return WorldOperationKind::Create;
    case WorldRunnerOp::EnterGame:
      return WorldOperationKind::EnterGame;
    case WorldRunnerOp::Shutdown:
      return WorldOperationKind::Shutdown;
    case WorldRunnerOp::Load:
    default:
      return WorldOperationKind::Load;
    }
  }();
  ProgressSink.Begin(kind);
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
    if (GameSession)
    {
      GameSession->SyncToWorldGameMode(World->GetGameMode());
      GameSession->SyncToWorldDifficulty(World->GetDifficulty());
    }
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
  CreateNewWorldWithSettings(settings, selection, view, WorldGameMode::Creative);
}

void UApplication::CreateNewWorldWithSettings(
    const ProceduralSettings &settings, const ResourcePackSelection &selection,
    const WorldViewSettings &view, WorldGameMode gameMode)
{
  CreateNewWorldWithSettings(settings, selection, view, gameMode,
                             WorldDifficulty::Normal);
}

void UApplication::CreateNewWorldWithSettings(
    const ProceduralSettings &settings, const ResourcePackSelection &selection,
    const WorldViewSettings &view, WorldGameMode gameMode,
    WorldDifficulty difficulty)
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
  request.gameMode = gameMode;
  request.difficulty = difficulty;
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
  const bool ok = Core->ApplyViewSettingsToCurrentWorld(view);
  if (ok)
  {
    OnGameplayViewProjectionChanged();
  }
  return ok;
}

bool UApplication::ApplyWorldSettings(const ResourcePackSelection &selection,
                                      const WorldViewSettings &view)
{
  if (!Core || !World)
  {
    return false;
  }

  ResourcePackSelection packs = selection;
  if (packs.WorldgenOwner.empty() && !packs.Primary.empty())
  {
    packs.WorldgenOwner = packs.Primary.front();
  }

  const ResourcePackSelection current =
      Core->GetCurrentWorldResourcePackSelection();
  const bool packs_changed =
      packs.Primary != current.Primary ||
      packs.Secondary != current.Secondary ||
      packs.WorldgenOwner != current.WorldgenOwner;

  if (packs_changed)
  {
    if (!Core->ApplyResourcePacksInMemory(packs))
    {
      return false;
    }
    RefreshBlockCatalog();
  }

  if (!Core->ApplyViewSettingsInMemory(view))
  {
    return false;
  }
  if (!Core->PersistWorldMetadata())
  {
    return false;
  }
  OnGameplayViewProjectionChanged();
  return true;
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

void UApplication::CloseCreativePalette()
{
  if (!PaletteOpen)
  {
    return;
  }
  PaletteOpen = false;
  if (PaletteScreen)
  {
    PaletteScreen->SetVisible(false);
  }
  if (GuiContext)
  {
    GuiContext->ClearInputState();
  }
  SyncCursorVisibility();
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

  SurvivalInventoryScreen =
      std::make_unique<USurvivalInventoryScreen>(
          &GameSession->GetContentCatalog(), GameSession.get(), icons);
  SurvivalInventoryScreen->OnAttach(*GuiContext);
  SurvivalInventoryScreen->Build(*GuiContext);
  SurvivalInventoryScreen->SetVisible(false);

  CraftingScreen = std::make_unique<UCraftingScreen>(
      GameSession.get(), World.get(), icons);
  CraftingScreen->OnAttach(*GuiContext);
  CraftingScreen->Build(*GuiContext);
  CraftingScreen->SetVisible(false);

  AnvilScreen = std::make_unique<UAnvilScreen>(World.get(), icons);
  AnvilScreen->OnAttach(*GuiContext);
  AnvilScreen->Build(*GuiContext);
  AnvilScreen->SetVisible(false);

  DeathScreen = std::make_unique<UDeathScreen>();
  DeathScreen->OnAttach(*GuiContext);
  DeathScreen->Build(*GuiContext);
  DeathScreen->SetOnRespawn(
      [this]()
      {
        if (!World)
        {
          return;
        }
        World->SetPlayerDead(false);
        if (UCreature *player = World->GetPlayerCreature())
        {
          CreatureVitals &v = player->GetVitals();
          v.FillFull();
          v.fatalWounds = 0;
          const glm::vec3 spawn = World->GetSpawnPoint();
          player->SetBodyOrigin(
              glm::vec3(spawn.x, spawn.y - player->GetEyeOffset().y, spawn.z));
          player->GetLocomotion().SetMode(CreatureMovementMode::Walking);
        }
        if (auto camera = World->GetCurrentUserCamera())
        {
          camera->SetFreeMove(false);
        }
        DeathScreenOpen = false;
        if (DeathScreen)
        {
          DeathScreen->SetVisible(false);
        }
        SyncCursorVisibility();
      });
  DeathScreen->SetOnSpectate(
      [this]()
      {
        if (!World)
        {
          return;
        }
        if (UCreature *player = World->GetPlayerCreature())
        {
          player->GetLocomotion().SetMode(CreatureMovementMode::Flying);
        }
        if (auto camera = World->GetCurrentUserCamera())
        {
          camera->SetFreeMove(true);
        }
        DeathScreenOpen = false;
        if (DeathScreen)
        {
          DeathScreen->SetVisible(false);
        }
        SyncCursorVisibility();
      });
  DeathScreen->SetVisible(false);

  CharacterSheetScreen =
      std::make_unique<UCharacterSheetScreen>(
          GameSession.get(), CreaturePreviewRenderer.get(), icons);
  CharacterSheetScreen->OnAttach(*GuiContext);
  CharacterSheetScreen->Build(*GuiContext);
  CharacterSheetScreen->SetVisible(false);

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
         ConsoleOpen || PaletteOpen || WorldGenOpen || CharacterSheetOpen ||
         SurvivalInventoryOpen || CraftingOpen || AnvilOpen || DeathScreenOpen;
}

bool UApplication::BlocksGameMouseLook() const
{
  return State == AppState::InGame &&
         (FreeCursor || ConsoleOpen || PaletteOpen || WorldGenOpen ||
          CharacterSheetOpen || SurvivalInventoryOpen || CraftingOpen || AnvilOpen || DeathScreenOpen);
}

AppCursorPolicy UApplication::GetCursorPolicy() const
{
  if (UsesUiPointer())
  {
    return AppCursorPolicy::Free;
  }
  if (State == AppState::InGame)
  {
    if (World)
    {
      if (World->GetViewSettings().Projection ==
          WorldProjectionMode::OrthographicIsometric)
      {
        return AppCursorPolicy::ConfinedVisible;
      }
      if (auto cam = World->GetCurrentUserCamera())
      {
        if (cam->IsIsometricProjection())
        {
          return AppCursorPolicy::ConfinedVisible;
        }
      }
    }
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
    double fb_x = x;
    double fb_y = y;
    CursorWindowToFramebuffer(Window, x, y, fb_x, fb_y);
    if (auto camera = World->GetCurrentUserCamera())
    {
      camera->ResetMouseMove(fb_x, fb_y);
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

void UApplication::OnGameplayViewProjectionChanged()
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
      wm->CancelGameplayPointerInteraction();
    }
  }
#endif

  // Force ApplyCursorPolicy on the next Sync (Classic DISABLED must drop for
  // isometric free-cursor aim).
  LastCursorPolicy = AppCursorPolicy::CapturedHidden;

  const bool iso =
      World->GetViewSettings().Projection ==
          WorldProjectionMode::OrthographicIsometric ||
      (World->GetCurrentUserCamera() &&
       World->GetCurrentUserCamera()->IsIsometricProjection());

#ifndef __ANDROID__
  if (Window && iso && State == AppState::InGame && !UsesUiPointer())
  {
    CenterWindowCursor(Window);
  }
#endif

  SyncCursorVisibility();
  if (State == AppState::InGame && !UsesUiPointer())
  {
    SyncGameplayLookCapture();
  }

  auto camera = World->GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }

  if (iso)
  {
#ifndef __ANDROID__
    if (Window)
    {
      double x = 0.0;
      double y = 0.0;
      glfwGetCursorPos(Window, &x, &y);
      double fb_x = x;
      double fb_y = y;
      CursorWindowToFramebuffer(Window, x, y, fb_x, fb_y);
      camera->UpdatePointerAim(World, fb_x, fb_y);
    }
#else
    if (TouchBridge)
    {
      const glm::vec2 pos = TouchBridge->GetMousePosition();
      camera->UpdatePointerAim(World, pos.x, pos.y);
    }
#endif
  }
  else
  {
    glm::vec3 origin;
    glm::vec3 dir;
    if (camera->TryGetCenterViewRay(origin, dir))
    {
      World->UpdateIntersection(origin, dir);
    }
  }
}

void UApplication::SyncCursorVisibility()
{
  const AppCursorPolicy policy = GetCursorPolicy();
  const bool policyChanged = policy != LastCursorPolicy;
  const bool leavingUiPointer = LastCursorPolicy == AppCursorPolicy::Free &&
                                policy != AppCursorPolicy::Free &&
                                State == AppState::InGame;
  const bool captureModeChanged =
      policyChanged && State == AppState::InGame &&
      (LastCursorPolicy == AppCursorPolicy::CapturedHidden ||
       policy == AppCursorPolicy::CapturedHidden);

#ifndef __ANDROID__
  if (Window)
  {
    ApplyCursorPolicy(Window, policy);
  }
#endif

  if (leavingUiPointer || captureModeChanged)
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
  CharacterSheetOpen = false;
  SurvivalInventoryOpen = false;
  CraftingOpen = false;
  AnvilOpen = false;
  DeathScreenOpen = false;
  FreeCursor = false;
  if (WorldGenScreen)
  {
    WorldGenScreen->SetVisible(false);
  }
  if (PaletteScreen)
  {
    PaletteScreen->SetVisible(false);
  }
  if (SurvivalInventoryScreen)
  {
    SurvivalInventoryScreen->SetVisible(false);
  }
  if (CraftingScreen)
  {
    CraftingScreen->SetVisible(false);
  }
  if (AnvilScreen)
  {
    AnvilScreen->SetVisible(false);
  }
  if (DeathScreen)
  {
    DeathScreen->SetVisible(false);
  }
  if (CharacterSheetScreen)
  {
    CharacterSheetScreen->SetVisible(false);
  }
#if defined(__ANDROID__)
  Ui.ControlScheme = ControlScheme::Cubatarium;
#endif
  GuiContext->ClearInputState();
  OnGameplayViewProjectionChanged();
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

void UApplication::FinishInventoryPointerGesture(const GuiMouseEvent &event)
{
  const int pointerIndex = NormalizeOverlayPointer(event.PointerId);

  if (GameSession && GameSession->IsDragging())
  {
    SlotAddress target;
    if (ResolveSlotAt(event.X, event.Y, target))
    {
      if (!GameSession->DropOnSlot(target))
      {
        GameSession->CancelDrag();
        GameSession->ClearPendingAssignment();
      }
    }
    else
    {
      GameSession->CancelDrag();
    }
  }

  if (OverlayPressedWidget)
  {
    OverlayPressedWidget->OnMouseUp(event);
    if (auto *slot = dynamic_cast<UGuiSlot *>(OverlayPressedWidget))
    {
      slot->ClearPressState();
    }
  }
  else
  {
    // Capture lost but Pressed may still be stuck on a slot under an overlay.
    const OverlayPointerCapture capture = OverlayCaptures[pointerIndex];
    auto routeUp = [&](UGuiWidget *root)
    {
      if (root)
      {
        root->OnMouseUp(event);
      }
    };
    switch (capture)
    {
    case OverlayPointerCapture::Palette:
      if (PaletteOpen && PaletteScreen)
      {
        routeUp(PaletteScreen->GetRoot());
      }
      break;
    case OverlayPointerCapture::SurvivalInventory:
      if (SurvivalInventoryOpen && SurvivalInventoryScreen)
      {
        routeUp(SurvivalInventoryScreen->GetRoot());
      }
      break;
    case OverlayPointerCapture::Crafting:
      if (CraftingOpen && CraftingScreen)
      {
        routeUp(CraftingScreen->GetRoot());
      }
      break;
    case OverlayPointerCapture::Anvil:
      if (AnvilOpen && AnvilScreen)
      {
        routeUp(AnvilScreen->GetRoot());
      }
      break;
    case OverlayPointerCapture::Death:
      if (DeathScreenOpen && DeathScreen)
      {
        routeUp(DeathScreen->GetRoot());
      }
      break;
    case OverlayPointerCapture::CharacterSheet:
      if (CharacterSheetOpen && CharacterSheetScreen)
      {
        routeUp(CharacterSheetScreen->GetRoot());
      }
      break;
    case OverlayPointerCapture::WorldGen:
      if (WorldGenOpen && WorldGenScreen)
      {
        routeUp(WorldGenScreen->GetRoot());
      }
      break;
    case OverlayPointerCapture::Console:
      if (ConsoleOpen && ConsoleScreen)
      {
        routeUp(ConsoleScreen->GetRoot());
      }
      break;
    case OverlayPointerCapture::Hud:
      routeUp(HudScreen ? HudScreen->GetRoot() : nullptr);
#if defined(__ANDROID__)
      ReleaseHudJoystickCaptureForPointer(event.PointerId);
#endif
      break;
    default:
      break;
    }
  }

  OverlayCaptures[pointerIndex] = OverlayPointerCapture::None;
  OverlayPressedWidget = nullptr;
}

bool UApplication::TryRouteInGameOverlay(const GuiMouseEvent &event,
                                         bool Pressed)
{
  const int pointerIndex = NormalizeOverlayPointer(event.PointerId);

  auto routeRootDown = [&](UGuiWidget *root) -> UGuiWidget *
  {
    if (!root)
    {
      return nullptr;
    }
    UGuiWidget *hit = root->HitTest(event.X, event.Y);
    if (!hit)
    {
      return nullptr;
    }
    if (!root->OnMouseDown(event))
    {
      return nullptr;
    }
    return hit;
  };

  if (Pressed)
  {
    OverlayPressedWidget = nullptr;
#if defined(__ANDROID__)
    if (HudScreen && HudScreen->HitTestTouchControls(event.X, event.Y))
    {
      if (UGuiWidget *hit = routeRootDown(HudScreen->GetRoot()))
      {
        OverlayCaptures[pointerIndex] = OverlayPointerCapture::Hud;
        OverlayPressedWidget = hit;
        return true;
      }
    }
#endif
    if (DeathScreenOpen && DeathScreen)
    {
      if (UGuiWidget *hit = routeRootDown(DeathScreen->GetRoot()))
      {
        OverlayCaptures[pointerIndex] = OverlayPointerCapture::Death;
        OverlayPressedWidget = hit;
        return true;
      }
    }
    if (WorldGenOpen)
    {
      if (UGuiWidget *hit = routeRootDown(WorldGenScreen->GetRoot()))
      {
        OverlayCaptures[pointerIndex] = OverlayPointerCapture::WorldGen;
        OverlayPressedWidget = hit;
        return true;
      }
    }
    if (PaletteOpen)
    {
      if (UGuiWidget *hit = routeRootDown(PaletteScreen->GetRoot()))
      {
        OverlayCaptures[pointerIndex] = OverlayPointerCapture::Palette;
        OverlayPressedWidget = hit;
        return true;
      }
    }
    if (SurvivalInventoryOpen && SurvivalInventoryScreen)
    {
      if (UGuiWidget *hit =
              routeRootDown(SurvivalInventoryScreen->GetRoot()))
      {
        OverlayCaptures[pointerIndex] = OverlayPointerCapture::SurvivalInventory;
        OverlayPressedWidget = hit;
        return true;
      }
    }
    if (CraftingOpen && CraftingScreen)
    {
      if (UGuiWidget *hit = routeRootDown(CraftingScreen->GetRoot()))
      {
        OverlayCaptures[pointerIndex] = OverlayPointerCapture::Crafting;
        OverlayPressedWidget = hit;
        return true;
      }
    }
    if (AnvilOpen && AnvilScreen)
    {
      if (UGuiWidget *hit = routeRootDown(AnvilScreen->GetRoot()))
      {
        OverlayCaptures[pointerIndex] = OverlayPointerCapture::Anvil;
        OverlayPressedWidget = hit;
        return true;
      }
    }
    if (CharacterSheetOpen && CharacterSheetScreen)
    {
      if (UGuiWidget *hit = routeRootDown(CharacterSheetScreen->GetRoot()))
      {
        OverlayCaptures[pointerIndex] = OverlayPointerCapture::CharacterSheet;
        OverlayPressedWidget = hit;
        return true;
      }
    }
    if (ConsoleOpen)
    {
      if (UGuiWidget *hit = routeRootDown(ConsoleScreen->GetRoot()))
      {
        OverlayCaptures[pointerIndex] = OverlayPointerCapture::Console;
        OverlayPressedWidget = hit;
        return true;
      }
    }
    if (UGuiWidget *hit =
            routeRootDown(HudScreen ? HudScreen->GetRoot() : nullptr))
    {
      OverlayCaptures[pointerIndex] = OverlayPointerCapture::Hud;
      OverlayPressedWidget = hit;
      return true;
    }
    return false;
  }

  // Mouse-up without an explicit FinishInventoryPointerGesture call.
  {
    const bool hadGesture =
        OverlayPressedWidget != nullptr ||
        OverlayCaptures[pointerIndex] != OverlayPointerCapture::None ||
        (GameSession && GameSession->IsDragging());
    if (!hadGesture)
    {
      return false;
    }
    FinishInventoryPointerGesture(event);
    return true;
  }
}

bool UApplication::ResolveSlotAt(int x, int y, SlotAddress &out)
{
  // Hotbar targets first so Tools grid never swallows drops meant for hotbar.
  if (PaletteOpen && PaletteScreen &&
      PaletteScreen->PickHotbarStrip(x, y, out))
  {
    return true;
  }
  if (HudScreen && HudScreen->PickSlot(x, y, out))
  {
    return true;
  }
  if (CharacterSheetOpen && CharacterSheetScreen)
  {
    size_t armorSlot = 0;
    if (CharacterSheetScreen->PickArmorSlot(x, y, armorSlot))
    {
      out = SlotAddress{};
      out.surface = SlotSurface::CharacterArmor;
      out.slot = armorSlot;
      return true;
    }
    if (CharacterSheetScreen->PickOffhandSlot(x, y))
    {
      out = SlotAddress{};
      out.surface = SlotSurface::CharacterOffhand;
      return true;
    }
    if (CharacterSheetScreen->PickMainSlot(x, y) && GameSession)
    {
      out = SlotAddress{};
      out.surface = SlotSurface::Hotbar;
      out.bar = 0;
      out.slot = GameSession->GetSelectedSlot(0);
      return true;
    }
  }
  if (PaletteOpen && PaletteScreen && PaletteScreen->PickGridSlot(x, y, out))
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
  case InventoryEntryKind::Item:
    tex = IconSource->GetItemIconTexture(drag.entry.Id);
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

void UApplication::NotifyFpSwing(FpSwingKind kind)
{
  if (FpViewmodelRenderer)
  {
    FpViewmodelRenderer->NotifySwing(kind);
  }
}

void UApplication::NotifyFpUseVisual(const std::string &presetId, bool hold)
{
  if (FpViewmodelRenderer)
  {
    FpViewmodelRenderer->NotifyUseVisual(presetId, hold);
  }
}

void UApplication::ClearFpHeldVisual()
{
  if (FpViewmodelRenderer)
  {
    FpViewmodelRenderer->ClearHeldVisual();
  }
}

void UApplication::OpenCraftingScreen()
{
  if (!CraftingScreen)
  {
    return;
  }
  CraftingScreen->SetWorld(World.get());
  CraftingOpen = true;
  AnvilOpen = false;
  SurvivalInventoryOpen = false;
  PaletteOpen = false;
  WorldGenOpen = false;
  CharacterSheetOpen = false;
  if (AnvilScreen)
  {
    AnvilScreen->SetVisible(false);
  }
  if (SurvivalInventoryScreen)
  {
    SurvivalInventoryScreen->SetVisible(false);
  }
  if (PaletteScreen)
  {
    PaletteScreen->SetVisible(false);
  }
  if (WorldGenScreen)
  {
    WorldGenScreen->SetVisible(false);
  }
  if (CharacterSheetScreen)
  {
    CharacterSheetScreen->SetVisible(false);
  }
  CraftingScreen->SetVisible(true);
  SyncCursorVisibility();
}

void UApplication::OpenAnvilScreen()
{
  if (!AnvilScreen)
  {
    return;
  }
  AnvilScreen->SetWorld(World.get());
  AnvilOpen = true;
  CraftingOpen = false;
  SurvivalInventoryOpen = false;
  PaletteOpen = false;
  WorldGenOpen = false;
  CharacterSheetOpen = false;
  if (CraftingScreen)
  {
    CraftingScreen->SetVisible(false);
  }
  if (SurvivalInventoryScreen)
  {
    SurvivalInventoryScreen->SetVisible(false);
  }
  if (PaletteScreen)
  {
    PaletteScreen->SetVisible(false);
  }
  if (WorldGenScreen)
  {
    WorldGenScreen->SetVisible(false);
  }
  if (CharacterSheetScreen)
  {
    CharacterSheetScreen->SetVisible(false);
  }
  AnvilScreen->SetVisible(true);
  SyncCursorVisibility();
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
      WorldOpRunner->AccumulateEnterLoadMs(dt * 1000.0);
      if (WorldOpRunner->IsEnterGameGpuWarmupStage())
      {
        constexpr int kGpuWarmupMaxFrames = 24;
        constexpr int kGpuWarmupMinFrames = 3;
        // Era20: smaller per-frame mesh budget; GPU upload only on last ready
        // frame so EnterGameAfterWorldChange ≠ mega WarmupGreedy spike.
        // Era29: slightly higher streaming budget — SoftDefer/PendingLight on bar.
        constexpr int kGpuWarmupMeshBudget = 8;
        constexpr int kGpuWarmupStreamingBudget = 6;
        const int remaining = WorldOpRunner->EnterGameGpuWarmupFramesRemaining();
        const int frame = kGpuWarmupMaxFrames - remaining;
        if (Geometry && World)
        {
          if (frame == 0)
          {
            Geometry->ResetWorldRenderState();
            LogWorldLoadDiag("gpu_warmup_reset", *World);
            if (!World->IsEnterLitGateActive())
            {
              UEnterLitDiagnostics::BeginSession();
              World->BeginEnterLitGate();
            }
          }
          if (World->NeedsEnterGameMeshWarmup())
          {
            World->DrainEnterGameMeshWarmup(kGpuWarmupMeshBudget);
          }
          if (World->IsEnterLitGateActive())
          {
            World->TickEnterGateMeshDrain(kGpuWarmupStreamingBudget);
          }
          else if (ShouldRunEnterStreamingWarmupDespiteSpawnPrepared(
                       World->IsSpawnAreaPreparedByCooperativeLoad()))
          {
            World->TickEnterStreamingWarmup(kGpuWarmupStreamingBudget);
          }
          World->TickEnterFovLitPass(
              std::max(1, URuntimeTuning::Get().EnterFovLitCaptureBudget));
          const bool upload_ready =
              frame >= kGpuWarmupMinFrames - 1 &&
              !World->NeedsEnterGameMeshWarmup();
          if (upload_ready)
          {
            World->WarmupVisibleListAtCamera();
            // Era20/Era31: single GPU greedy warmup on last remaining frame —
            // remaining<=2 was stacking WarmupGreedy into enter_app hitch.
            if (remaining == 1)
            {
              Geometry->WarmupGreedyGpuFromWorld();
              LogWorldLoadDiag("gpu_warmup_draw", *World);
            }
          }
        }
        WorldOpRunner->AdvanceEnterGameGpuWarmup(ProgressSink, dt * 1000.0);
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
    if (SurvivalInventoryOpen && SurvivalInventoryScreen)
    {
      SurvivalInventoryScreen->Update(dt);
    }
    if (CraftingOpen && CraftingScreen)
    {
      CraftingScreen->Update(dt);
    }
    if (AnvilOpen && AnvilScreen)
    {
      AnvilScreen->Update(dt);
    }
    if (World && World->IsPlayerDead() && !DeathScreenOpen && DeathScreen)
    {
      DeathScreenOpen = true;
      DeathScreen->SetCause("Fatal wounds");
      DeathScreen->SetVisible(true);
      SyncCursorVisibility();
    }
    if (DeathScreenOpen && DeathScreen)
    {
      DeathScreen->Update(dt);
    }
    if (WorldGenScreen)
    {
      WorldGenScreen->Update(dt);
    }
    if (CharacterSheetOpen && CharacterSheetScreen)
    {
      CharacterSheetScreen->Update(dt);
    }
    if (FpViewmodelRenderer && World)
    {
      float yaw = 0.f;
      float pitch = 0.f;
      float speed = 0.f;
      if (auto cam = World->GetCurrentUserCamera())
      {
        yaw = cam->GetYaw();
        pitch = cam->GetPitch();
      }
      if (const UCreature *creature = World->GetControlledCreature())
      {
        speed = creature->GetLocomotionFacts().horizontalSpeed;
      }
      FpViewmodelRenderer->Update(static_cast<float>(dt), yaw, pitch, speed);
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
    {
      const WorldViewSettings &view = World->GetViewSettings();
      WornEquipmentDrawer::SetHidePossessedWield(ShouldDrawFpViewmodel(view));
      if (Core)
      {
        if (auto items = Core->GetItemDefinitionStorage())
        {
          WornEquipmentDrawer::SetItemDefinitions(items.get());
        }
      }
      Geometry->Paint(width, height, viewDuration);
      WornEquipmentDrawer::SetHidePossessedWield(false);
    }
  }

  if (State == AppState::InGame && World && !MinimalOverlayForBench &&
      !PaletteOpen && FpViewmodelRenderer)
  {
    const WorldViewSettings &view = World->GetViewSettings();
    if (ShouldDrawFpViewmodel(view))
    {
      if (const UCreature *creature = World->GetControlledCreature())
      {
        const auto &inv = creature->GetInventory();
        FpViewmodelDrawParams fpParams;
        fpParams.FramebufferW = width;
        fpParams.FramebufferH = height;
        fpParams.Active = inv.GetActiveEntryRef();
        fpParams.Offhand = &inv.GetEquippedOffhand();
        fpParams.SpeciesId = creature->GetTypeId();
        fpParams.SkinId = creature->GetSkinId();
        FpViewmodelRenderer->DrawWorldOverlay(fpParams);
      }
    }
  }

  const auto gui_begin = std::chrono::high_resolution_clock::now();
  const bool hitch_gui =
      World && (World->GetWallFrameDelta() * 1000.0) > 24.0;
  if (State == AppState::InGame && IconSource && !MinimalOverlayForBench &&
      !hitch_gui)
  {
    IconSource->WarmupObjectIcons(2);
    IconSource->WarmupCreatureIcons(2);
  }

  if (HudScreen && HudScreen->GetRoot() && !MinimalOverlayForBench)
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
  if (SurvivalInventoryOpen && SurvivalInventoryScreen &&
      SurvivalInventoryScreen->GetRoot())
  {
    notifyViewport(SurvivalInventoryScreen.get());
    GuiContext->RenderOverlay(*SurvivalInventoryScreen->GetRoot(), width,
                               height, false);
  }
  if (CraftingOpen && CraftingScreen && CraftingScreen->GetRoot())
  {
    notifyViewport(CraftingScreen.get());
    GuiContext->RenderOverlay(*CraftingScreen->GetRoot(), width, height, false);
  }
  if (AnvilOpen && AnvilScreen && AnvilScreen->GetRoot())
  {
    notifyViewport(AnvilScreen.get());
    GuiContext->RenderOverlay(*AnvilScreen->GetRoot(), width, height, false);
  }
  if (DeathScreenOpen && DeathScreen && DeathScreen->GetRoot())
  {
    notifyViewport(DeathScreen.get());
    GuiContext->RenderOverlay(*DeathScreen->GetRoot(), width, height, false);
  }
  if (CharacterSheetOpen && CharacterSheetScreen &&
      CharacterSheetScreen->GetRoot())
  {
    notifyViewport(CharacterSheetScreen.get());
    GuiContext->RenderOverlay(*CharacterSheetScreen->GetRoot(), width, height,
                               false);
  }
  if (WorldGenOpen && WorldGenScreen && WorldGenScreen->GetRoot())
  {
    WorldGenScreen->RenderPreview();
    notifyViewport(WorldGenScreen.get());
    GuiContext->RenderOverlay(*WorldGenScreen->GetRoot(), width, height, false);
  }
#if defined(__ANDROID__)
  if (HudScreen && (PaletteOpen || WorldGenOpen || SurvivalInventoryOpen ||
                    CraftingOpen || AnvilOpen))
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
  return ConsoleOpen || PaletteOpen || WorldGenOpen || CharacterSheetOpen ||
         SurvivalInventoryOpen || CraftingOpen || AnvilOpen || DeathScreenOpen ||
         GuiContext->WantsCaptureKeyboard();
}

bool UApplication::AllowsWorldMousePlacement() const
{
  return State == AppState::InGame && !ConsoleOpen && !PaletteOpen &&
         !WorldGenOpen && !CharacterSheetOpen && !SurvivalInventoryOpen && !CraftingOpen && !AnvilOpen &&
         !DeathScreenOpen;
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
  CreatureHabitat habitat = CreatureHabitat::Terrestrial;
  if (UCreature *controlled = World->GetControlledCreature())
  {
    if (const CreatureDefinition *def =
            World->GetCreatureDefinition(controlled->GetTypeId()))
    {
      habitat = def->habitat;
    }
  }
  if (!ModePolicy::AllowsFlight(World->GetGameMode(), habitat))
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
