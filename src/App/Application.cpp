#include "App/StoredApplicationActions.h"
#include "World/Core/WorldOwnedCallbacks.h"
#include "App/Application.h"
#include "App/Platform/IUPlatformPaths.h"
#include "Core/StallTimer.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Game/Inventory/HotbarInput.h"
#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Test/Native/ItemLabArtQa.h"
#include "Test/Native/ItemLabDiagnosticsQa.h"
#include "Test/Native/ItemLabSceneQa.h"
#include "Test/Native/ItemPerformanceQa.h"
#include "Test/Native/ItemPresentationQa.h"
#include "Test/Native/ItemWorldVisualQa.h"
#include "Test/Native/ItemsInventoryQa.h"
#include "Test/Native/OwnershipQa.h"
#include "Test/Native/PlayerWindowCapture.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Diagnostics/EnterLitDiagnostics.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include <set>
#include <thread>

#include "App/Core.h"
#include "App/Platform/CursorCapture.h"
#include "App/Platform/Log.h"
#include "Audio/AudioApi.h"
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
#include "App/Platform/Log.h"
#include "App/WorldOperationRunner.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Player/User.h"
#include "Creatures/Stats/CreatureVitals.h"
#include "Creatures/Visual/CarryPresentation.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Game/GameSession.h"
#include "Game/ModePolicy.h"
#include "Gui/Cache/CreatureIconCache.h"
#include "Gui/Cache/CreatureItemIconStorage.h"
#include "Gui/Cache/InventoryIconService.h"
#include "Gui/Cache/ItemIconCache.h"
#include "Gui/Cache/ObjectIconCache.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiIconSource.h"
#include "Items/ItemGltfTextureCache.h"
#include "Gui/Core/GuiMetrics.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiScale.h"
#include "Gui/Core/GuiTypes.h"
#include "Gui/Interfaces/IUGuiClipboard.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Gui/Preview/ItemLabSceneRenderer.h"
#include "Gui/Preview/ItemPreviewRenderer.h"
#include "Gui/Screens/AnvilScreen.h"
#include "Gui/Screens/CharacterSheetScreen.h"
#include "Gui/Screens/CraftingScreen.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Screens/DeathScreen.h"
#include "Gui/Screens/InGameHudScreen.h"
#include "Gui/Screens/ItemLabScreen.h"
#include "Gui/Screens/LoadWorldScreen.h"
#include "Gui/Screens/MainMenuScreen.h"
#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Screens/SettingsScreen.h"
#include "Gui/Screens/WorldProgressScreen.h"
#include "Gui/Screens/WorldResourcePacksScreen.h"
#include "Gui/Widgets/GuiPopupMenu.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Items/FpViewmodelRenderer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemPresentationMath.h"
#include "Render/Engine/EnvironmentLight.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/TextRenderer.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Textures/TextureCube.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/World.h"
#include "World/Core/WorldLoadDiagnostics.h"
#include "World/Lighting/LightUtil.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/View/WorldViewSettings.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"

#ifndef __ANDROID__
#include <GLFW/glfw3.h>
#else
#include "App/Platform/GlfwKeyCompat.h"
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace cutum
{

namespace
{
std::unique_ptr<UItemPerformanceQa> ItemPerformanceQa;
std::unique_ptr<UItemWorldVisualQa> ItemWorldVisualQa;

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
    World->SetOnBlockRegistryChanged(ApplicationBlockCatalogCallback{this});
    World->SetOnCreatureCatalogChanged(ApplicationCreatureCatalogCallback{this});
  }
}

UApplication::~UApplication() { PrepareForShutdown(); }

void UApplication::PrepareForShutdown()
{
  CubatariumSetSuppressErrorDialogs(true);
  WorldOpRunner.reset();
  IconSource.reset();
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
  StallStageMark("before-Startup");
  if (Core)
  {
    Core->LoadConfig(configPath);
    StallStageMark("Startup.LoadConfig");

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
  StallStageMark("Startup.GuiContext.Initialize");
  if (Core && BlockDefinitions)
  {
    const std::string typesPath = "content/types.json";
    if (!UWorldGenRefs::LoadFromFile("content/worldgen_refs.json"))
    {
      CubatariumLogInfo("App", "worldgen_refs.json not loaded — using legacy "
                               "block name resolution");
    }
    if (!UObjectFeatureConfigStorage::LoadFromFile(
            "content/object_features.json"))
    {
      CubatariumLogInfo(
          "App",
          "object_features.json not loaded — legacy tree placement only");
    }
    if (!UWorldGenPack::LoadPackId("default"))
    {
      CubatariumLogInfo(
          "App", "worldgen pack not loaded — built-in biome defaults only");
    }
    GameSession->InitializeCatalog(typesPath, *Core);
  }
  StallStageMark("Startup.worldgen+catalog");
  GameSession->RegisterCommands();

  if (Core && BlockDefinitions && ShaderManager)
  {
    auto textures = Core->GetTextureCubeStorage();
    std::shared_ptr<UCreaturePreviewRenderer> creaturePreview;
    if (World)
    {
      creaturePreview = UCreaturePreviewRenderer::Create(
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
      itemPreview = UItemPreviewRenderer::Create(
          Core->GetItemDefinitionStorage(), ShaderManager);
      if (!itemPreview->Initialize())
      {
        itemPreview.reset();
      }
    }

    auto iconService = UInventoryIconService::Create();
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

  StallStageMark("Startup.renderers+icons");
  State = AppState::MainMenu;
  ShowMainMenu();
  StallStageMark("Startup.ShowMainMenu");
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
  if (State == AppState::Loading)
    return; // The active operation still owns unsaved data and completion.
  if (!Core || !World)
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
                      ApplicationShutdownCompleteAction{this});
}

bool UApplication::TryBeginShutdownFromWindowClose()
{
  if (!Core || !World)
  {
    return false;
  }
  if (State == AppState::Loading)
  {
    return true; // Consume repeated close; never bypass the active save.
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

  MainMenuScreen = nullptr;
  GuiContext->SetScreen(nullptr);
  State = AppState::InGame;
  EnterInGameInputState();
}

void UApplication::ShowWorldProgressScreen()
{
  MainMenuScreen = nullptr;
  ConsoleOpen = false;
  PaletteOpen = false;
  FreeCursor = true;
  auto screen = std::make_unique<UWorldProgressScreen>();
  auto *progress = screen.get();
  ProgressScreen = nullptr;
  GuiContext->SetScreen(std::move(screen));
  ProgressScreen = progress;
  SyncCursorVisibility();
}

void UApplication::BeginWorldOperation(WorldRunnerRequest request,
                                       std::function<void()> onComplete)
{
  PendingFirstWorldPaint = false;
  FirstWorldPaintWaitMs = 0.0;
  FirstWorldPaintRepairFrames = 0;
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
  if (request.op == WorldRunnerOp::EnterGame)
  {
    World->SetEnterSessionPhase(EnterSessionPhase::CooperativeLoad);
  }
  WorldOpRunner->Start(std::move(request));
}

void UApplication::ShowFatalSaveFailure(const std::string &message)
{
  WorldOpOnComplete = nullptr;
  State = AppState::MainMenu;
  ShowWorldProgressScreen();
  ProgressSink.Begin(WorldOperationKind::Save);
  ProgressSink.End(false, message);
  if (ProgressScreen)
  {
    ProgressScreen->SetFailureActions(
        ApplicationScheduleRetrySaveAction{this}, ApplicationScheduleShowMenuAction{this});
    ProgressScreen->ApplySnapshot(ProgressSink.Get());
  }
  SyncCursorVisibility();
}

void UApplication::OnWorldOperationFinished()
{
  const bool success = WorldOpRunner && WorldOpRunner->Succeeded();
  if (WorldOpRunner && WorldOpRunner->HasSaveFailure())
  {
    if (World)
      World->ResumeAfterSessionSave();
    WorldOpOnComplete = nullptr;
    State = AppState::MainMenu;
    FreeCursor = true;
    if (ProgressScreen)
    {
      ProgressScreen->SetFailureActions(
          ApplicationScheduleRetrySaveAction{this}, ApplicationScheduleShowMenuAction{this});
      ProgressScreen->ApplySnapshot(ProgressSink.Get());
    }
    else
      ShowMainMenu();
    return;
  }
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
    if (World)
    {
      World->SetEnterSessionPhase(EnterSessionPhase::None);
    }
    ShowMainMenu();
    State = AppState::MainMenu;
    WorldOpOnComplete = nullptr;
    return;
  }

  if (WorldOpRunner && WorldOpRunner->ShouldEnterGame())
  {
    World->PrepareEnterGameSession();
    if (const char *probe = std::getenv("CUBA_FLIGHT_QA_FIRST_PAINT_DEBT");
        probe && std::string(probe) == "1")
    {
      const auto focus = UChunkManager::WorldToChunk(World->GetPreferredLoadFocusBlock());
      const auto &settings = World->GetProceduralSettings();
      bool injected = false;
      for (int cy = FloorDiv(settings.MinHeight, CHUNK_SIZE);
           cy <= FloorDiv(settings.MaxHeight, CHUNK_SIZE); ++cy)
      {
        const glm::ivec3 coord(focus.x,cy,focus.z);
        if (!World->HasCurrentChunkSlicePublication(coord) ||
            !World->GetMeshService().HasDrawableGreedyMesh(coord))
          continue;
        World->GetMeshService().RemoveColumn({focus.x,0,focus.z},cy,cy);
        if (World->HasCurrentChunkSlicePublication(coord))
          throw std::runtime_error("First-paint probe failed to retire publication");
        CubatariumLogInfo("FirstPaintBarrierProbe", "publication_retired coord=(" +
            std::to_string(coord.x) + "," + std::to_string(coord.y) + "," +
            std::to_string(coord.z) + ") voxel_data_preserved=1");
        injected = true;
        break;
      }
      if (!injected)
        throw std::runtime_error("First-paint probe requires a current drawable section");
    }
    PendingFirstWorldPaint = true;
    FirstWorldPaintWaitMs = 0.0;
    FirstWorldPaintRepairFrames = 0;
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
  PendingFirstWorldPaint = false;
  ScreenNav.ShowMainMenu();
}

MainMenuStorage UApplication::InspectMainMenuStorage() const noexcept
{
  if (!GuiContext)
    return {};
  const auto *screen = dynamic_cast<const UMainMenuScreen *>(GuiContext->GetScreen());
  if (screen != MainMenuScreen)
  {
    MainMenuStorage result;
    result.present = screen || MainMenuScreen;
    return result;
  }
  if (screen)
    return screen->InspectOwnedStorage(*GuiContext, GameSession.get());
  MainMenuStorage result;
  result.bindings_match = true;
  result.supported = true;
  return result;
}

InGameHudStorage UApplication::InspectHudStorage() const noexcept
{
  if (HudScreen && GuiContext)
    return HudScreen->InspectOwnedStorage(*GuiContext, GameSession.get(), IconSource.get());
  InGameHudStorage result;
  result.present = bool(HudScreen);
  result.bindings_match = !HudScreen;
  result.supported = !HudScreen;
  return result;
}

ConsoleStorage UApplication::InspectConsoleStorage() const noexcept
{
  if (ConsoleScreen && GuiContext)
    return ConsoleScreen->InspectOwnedStorage(*GuiContext, GameSession.get(), OverlayPopup.get());
  ConsoleStorage result;
  result.present = bool(ConsoleScreen) || bool(OverlayPopup);
  result.bindings_match = !result.present;
  result.supported = !result.present;
  return result;
}

DeathScreenStorage UApplication::InspectDeathScreenStorage() const noexcept
{
  if (DeathScreen && GuiContext)
  {
    auto out = DeathScreen->InspectOwnedStorage(*GuiContext, *this);
    out.bindings_match &= out.visible == DeathScreenOpen;
    out.supported &= out.bindings_match;
    return out;
  }
  DeathScreenStorage out;
  out.present = bool(DeathScreen);
  out.bindings_match = !out.present && !DeathScreenOpen;
  out.supported = out.bindings_match;
  return out;
}

PlayerWindowStorage UApplication::InspectPlayerWindowStorage() const noexcept
{
  if (PlayerWindowScreen && GuiContext)
    return PlayerWindowScreen->InspectOwnedStorage(*GuiContext, GameSession.get(),
        CharacterSheetScreen.get(), PaletteScreen.get(), IconSource.get());
  PlayerWindowStorage out;
  out.present = PlayerWindowScreen || CharacterSheetScreen || PaletteScreen;
  out.bindings_match = !out.present;
  out.supported = out.bindings_match;
  return out;
}

PaletteGridStorage UApplication::InspectPaletteGridStorage() const noexcept
{
  if (PaletteScreen && GameSession)
    return PaletteScreen->InspectGridStorage(GameSession->GetContentCatalog(), *GameSession);
  PaletteGridStorage out;
  out.supported = !PaletteScreen;
  return out;
}

WorldGenPaletteStorage UApplication::InspectWorldGenPaletteStorage() const noexcept
{
  if (WorldGenScreen && GuiContext && GameSession)
  {
    auto out = WorldGenScreen->InspectOwnedStorage(*GuiContext, World.get(),
        &GameSession->GetContentCatalog(), IconSource.get(), ContentPreviewRenderer.get());
    out.bindingsMatch &= out.visible == WorldGenOpen;
    out.supported &= out.bindingsMatch;
    return out;
  }
  WorldGenPaletteStorage out;
  out.present = bool(WorldGenScreen);
  out.bindingsMatch = !out.present && !WorldGenOpen;
  out.supported = out.bindingsMatch;
  return out;
}

ContentPreviewRendererStorage UApplication::InspectContentPreviewStorage() const noexcept
{
  if (ContentPreviewRenderer && Core)
    return ContentPreviewRenderer->InspectOwnedStorage(Core->GetObjectLibrary().get(),
        Core->GetTextureCubeStorage().get(), BlockDefinitions.get(), ShaderManager.get(),
        CreaturePreviewRenderer.get());

  ContentPreviewRendererStorage out;
  out.present = bool(ContentPreviewRenderer);
  out.bindingsMatch = !out.present;
  out.supported = out.bindingsMatch;
  return out;
}

ObjectIconStorage UApplication::InspectObjectIconStorage() const noexcept
{
  if (IconSource && Core)
    return IconSource->InspectObjectIconStorage(Core->GetObjectLibrary().get(),
        Core->GetTextureCubeStorage().get(), BlockDefinitions.get(), ShaderManager.get());

  ObjectIconStorage out;
  out.bindingsMatch = !IconSource;
  out.supported = out.bindingsMatch;
  return out;
}

InventoryIconStorage UApplication::InspectInventoryIconStorage() const noexcept
{
  if (IconSource)
    return IconSource->InspectInventoryIconStorage();
  InventoryIconStorage out;
  out.bindingsMatch = out.supported = true;
  return out;
}

void UApplication::InspectRenderImages(RenderImageCensus &out) const
{
  if (TextRenderer)
    TextRenderer->InspectRenderImages(out);
  if (ContentPreviewRenderer)
    ContentPreviewRenderer->InspectRenderImages(out);
  if (ItemLabScreen)
    ItemLabScreen->InspectRenderImages(out);
  if (IconSource)
    IconSource->InspectRenderImages(out);
  ItemGltfTextureCache::InspectRenderImages(out);
}

void UApplication::InspectGuiGpuBuffers(TerrainGpuBufferCensus &out) const
{
  GuiContext->InspectGpuBuffers(out);
  if (TextRenderer)
    TextRenderer->InspectGpuBuffers(out);
}

void UApplication::InspectVisualAssets(VisualAssetCensus &census) const
{
  if (CreaturePreviewRenderer)
    CreaturePreviewRenderer->InspectVisualAssets(census);
  if (ContentPreviewRenderer)
    ContentPreviewRenderer->InspectVisualAssets(census, CreaturePreviewRenderer.get());
  if (FpViewmodelRenderer)
    FpViewmodelRenderer->InspectVisualAssets(census);
  if (ItemLabScreen)
    ItemLabScreen->InspectVisualAssets(census);
}

CreatureItemIconStorage UApplication::InspectCreatureItemIconStorage() const noexcept
{
  return UGuiIconSource::InspectCreatureItemIconStorage(IconSource.get(), ContentPreviewRenderer.get(),
      CreaturePreviewRenderer, World ? World->GetCreatureDefinitionStorage().get() : nullptr,
      World ? World->GetSkinDefinitionStorage().get() : nullptr,
      Core ? Core->GetCreatureTextureStorage().get() : nullptr,
      Core ? Core->GetItemDefinitionStorage().get() : nullptr, ShaderManager.get());
}

void UApplication::SetHotbarCountSetting(int count)
{
  Ui.HotbarCount = std::clamp(count, 1, 4);
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
  ItemLabOpen = false;
  ItemLabScreen = nullptr;
  DeathSpectating = false;
  SetDeathOverlayVisible(false);
  ScreenNav.ReturnToMainMenu();
}

void UApplication::ShowSettings() { ScreenNav.ShowSettings(); }

void UApplication::ShowWorldSettings() { ScreenNav.ShowWorldSettings(); }

void UApplication::ShowNewWorld() { ScreenNav.ShowNewWorld(); }

void UApplication::ShowLoadWorld() { ScreenNav.ShowLoadWorld(); }

void UApplication::SaveWorldSessionIfNeeded() { SaveActiveWorldIfNeeded(); }

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
  if (FpViewmodelRenderer)
    FpViewmodelRenderer->ClearHeldVisual();
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
  if (World)
  {
    World->SetEnterSessionPhase(EnterSessionPhase::Done);
  }
  EnterInGameInputState();
}

void UApplication::ScheduleDeferredMenuAction(std::function<void()> Action)
{
  PendingMenuAction = std::move(Action);
}

void UApplication::RunPendingMenuAction()
{
  if (PendingMenuAction)
  {
    auto Action = std::move(PendingMenuAction);
    PendingMenuAction = nullptr;
    Action();
  }
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
  UpdateUiScale(LastFramebufferWidth, LastFramebufferHeight,
                LastPlatformMetrics);
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
  CreateNewWorldWithSettings(settings, selection, view,
                             WorldGameMode::Creative);
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

ResourcePackSelection UApplication::GetCurrentWorldResourcePackSelection() const
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

ThrowSettings UApplication::GetCurrentThrowSettings() const
{
  return World ? World->GetThrowSettings() : ThrowSettings{};
}
bool UApplication::ApplyThrowSettings(const ThrowSettings &settings)
{
  if (!World)
    return false;
  World->SetThrowSettings(settings);
  World->PersistWorldMetadata();
  return true;
}
WorldViewSettings UApplication::GetCurrentWorldViewSettings() const
{
  return Core ? Core->GetCurrentWorldViewSettings() : WorldViewSettings{};
}

bool UApplication::ApplyViewSettingsToCurrentWorld(
    const WorldViewSettings &view)
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
  const bool packs_changed = packs.Primary != current.Primary ||
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
  MainMenuScreen = nullptr;
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
  auto hud = std::make_unique<UInGameHudScreen>(GameSession.get(),
                                                &GuiContext->GetTheme(), icons);
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

  auto popup = std::make_unique<UGuiPopupMenu>(&GuiContext->GetTheme());
  auto console = std::make_unique<UConsoleScreen>(GameSession.get());
  console->OnAttach(*GuiContext);
  console->Build(*GuiContext);
  console->AttachPopup(popup.get());
  console->SetVisible(false);
  OverlayPopup = std::move(popup);
  ConsoleScreen = std::move(console);

  auto palette = std::make_unique<UCreativePaletteScreen>(
      &GameSession->GetContentCatalog(), GameSession.get(), icons, preview);
  palette->OnAttach(*GuiContext);
  palette->Build(*GuiContext);
  palette->SetVisible(false);

  auto crafting =
      std::make_unique<UCraftingScreen>(GameSession.get(), World.get(), icons);
  crafting->OnAttach(*GuiContext);
  crafting->Build(*GuiContext);
  CloseCraftingScreen();
  CraftingScreen = std::move(crafting);

  auto anvil = std::make_unique<UAnvilScreen>(World.get(), icons);
  anvil->OnAttach(*GuiContext);
  anvil->Build(*GuiContext);
  CloseAnvilScreen();
  AnvilScreen = std::move(anvil);

  BuildDeathOverlay();

  auto character = std::make_unique<UCharacterSheetScreen>(
      GameSession.get(), CreaturePreviewRenderer.get(), icons);
  character->OnAttach(*GuiContext);
  character->Build(*GuiContext);
  character->SetVisible(false);
  auto playerWindow = std::make_unique<UPlayerWindowScreen>(
      GameSession.get(), character.get(), palette.get(), icons);
  playerWindow->OnAttach(*GuiContext);
  playerWindow->Build(*GuiContext);
  character->SetVisible(false);
  for (auto &capture : OverlayCaptures)
  {
    if (capture == OverlayPointerCapture::CharacterSheet || capture == OverlayPointerCapture::Palette)
    {
      capture = OverlayPointerCapture::None;
      OverlayPressedWidget = nullptr;
    }
  }
  PlayerWindowScreen = std::move(playerWindow);
  CharacterSheetScreen = std::move(character);
  PaletteScreen = std::move(palette);

  BuildWorldGenOverlay();
  GuiContext->SetScreen(nullptr);
}

void UApplication::BuildWorldGenOverlay()
{
  auto screen = std::make_unique<UWorldGenPaletteScreen>(World.get(),
      &GameSession->GetContentCatalog(), IconSource.get(), ContentPreviewRenderer.get());
  screen->OnAttach(*GuiContext);
  screen->Build(*GuiContext);
  CloseWorldGenScreen();
  WorldGenScreen = std::move(screen);
}

void UApplication::BuildDeathOverlay()
{
  auto screen = std::make_unique<UDeathScreen>(*this);
  screen->OnAttach(*GuiContext);
  screen->Build(*GuiContext);
  for (auto &capture : OverlayCaptures)
  {
    if (capture == OverlayPointerCapture::Death)
    {
      capture = OverlayPointerCapture::None;
      OverlayPressedWidget = nullptr;
    }
  }
  DeathScreen = std::move(screen);
  DeathScreenOpen = false;
  DeathSpectating = false;
}

void UApplication::SetDeathOverlayVisible(bool visible)
{
  if (DeathScreen)
    DeathScreen->SetVisible(visible);
  DeathScreenOpen = visible && bool(DeathScreen);
}

void UApplication::UpdateDeathOverlay()
{
  if (!World || !World->IsPlayerDead())
  {
    DeathSpectating = false;
    if (DeathScreenOpen)
    {
      SetDeathOverlayVisible(false);
      SyncCursorVisibility();
    }
    return;
  }
  if (!DeathScreen || DeathScreenOpen || DeathSpectating)
    return;

  DeathScreen->SetHardcore(World->IsHardcoreDead());
  DeathScreen->SetCause({});
  SetDeathOverlayVisible(true);
  SyncCursorVisibility();
}

void UApplication::RespawnPlayerFromDeathScreen()
{
  if (!World || !ModePolicy::AllowsPlayerRespawn(World->GetDifficulty()))
    return;

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
    camera->SetFreeMove(false);
  DeathSpectating = false;
  SetDeathOverlayVisible(false);
  SyncCursorVisibility();
}

void UApplication::SpectateFromDeathScreen()
{
  if (!World)
    return;

  if (UCreature *player = World->GetPlayerCreature())
    player->GetLocomotion().SetMode(CreatureMovementMode::Flying);
  if (auto camera = World->GetCurrentUserCamera())
    camera->SetFreeMove(true);
  DeathSpectating = true;
  SetDeathOverlayVisible(false);
  SyncCursorVisibility();
}

void UApplication::ScheduleReturnFromDeathScreen()
{
  SaveIfNeededAndProceed(ApplicationReturnMenuAction{this});
}

bool UApplication::UsesUiPointer() const
{
  return State == AppState::MainMenu || State == AppState::Loading ||
         FreeCursor || ItemLabOpen || ConsoleOpen || PaletteOpen ||
         WorldGenOpen || CharacterSheetOpen ||
         CraftingOpen || AnvilOpen || DeathScreenOpen;
}

bool UApplication::BlocksGameMouseLook() const
{
  return State == AppState::InGame &&
         (FreeCursor || ItemLabOpen || ConsoleOpen || PaletteOpen ||
          WorldGenOpen || CharacterSheetOpen ||
          CraftingOpen || AnvilOpen || DeathScreenOpen);
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

  const bool iso = World->GetViewSettings().Projection ==
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
  // Focus loss mutes the whole mixer; nothing changes in the config.
  if (Core)
  {
    Core->SetAudioFocusMuted(!focused);
  }
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
  CloseWorldGenScreen();
  CharacterSheetOpen = false;
  CloseCraftingScreen();
  CloseAnvilScreen();
  FreeCursor = false;
  if (PaletteScreen)
  {
    if (std::find(OverlayCaptures.begin(), OverlayCaptures.end(), OverlayPointerCapture::Palette) != OverlayCaptures.end())
    {
      if (auto *slot = dynamic_cast<UGuiSlot *>(OverlayPressedWidget))
        slot->ClearPressState();
      OverlayPressedWidget = nullptr;
      std::replace(OverlayCaptures.begin(), OverlayCaptures.end(), OverlayPointerCapture::Palette,
                   OverlayPointerCapture::None);
      GameSession->CancelDrag();
    }
    PaletteScreen->SetPointerPressed(false);
    PaletteScreen->SetVisible(false);
  }
  SetDeathOverlayVisible(false);
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

void UApplication::ShowInteractionMessage(const std::string &message,
                                          float seconds)
{
  if (Geometry)
    Geometry->ShowTransientMessage(message, seconds);
}
void UApplication::OpenPlayerWindow(int section)
{
  if (!World || !GameSession)
    return;
  GameSession->CancelDrag();
  GameSession->ClearPendingAssignment();
  const bool catalog =
      section == 4 && World->GetGameMode() == WorldGameMode::Creative;
  if (PlayerWindowScreen)
    PlayerWindowScreen->SelectSection(catalog ? 4 : std::clamp(section, 0, 3));
  PaletteOpen = catalog;
  CharacterSheetOpen = !catalog;
  CloseWorldGenScreen();
  CloseCraftingScreen();
  CloseAnvilScreen();
  SyncCursorVisibility();
}
UGuiWidget *UApplication::GetPlayerWindowRoot()
{
  if (!PlayerWindowScreen)
    return nullptr;
  PlayerWindowScreen->SelectSection(
      PaletteOpen ? 4 : CharacterSheetScreen->GetSection());
  return PlayerWindowScreen->GetRoot();
}
void UApplication::FinishInventoryPointerGesture(const GuiMouseEvent &event)
{
  const int pointerIndex = NormalizeOverlayPointer(event.PointerId);

  const bool completedDrag = GameSession && GameSession->IsDragging();
  SlotAddress releaseTarget;
  const bool sameSource =
      completedDrag && ResolveSlotAt(event.X, event.Y, releaseTarget) &&
      (releaseTarget.surface == GameSession->GetDrag().source.surface &&
       releaseTarget.bar == GameSession->GetDrag().source.bar &&
       releaseTarget.slot == GameSession->GetDrag().source.slot);
  const bool buttonAction =
      dynamic_cast<UGuiButton *>(OverlayPressedWidget) != nullptr;
  if (completedDrag && !sameSource && !buttonAction)
  {
    SlotAddress target;
    if (ResolveSlotAt(event.X, event.Y, target))
    {
      if (!GameSession->DropOnSlot(
              target, event.Button == GuiMouseButton::Right ? 1 : -1))
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
    if (completedDrag && !sameSource)
      if (auto *slot = dynamic_cast<UGuiSlot *>(OverlayPressedWidget))
        slot->ClearPressState();
    OverlayPressedWidget->OnMouseUp(event);
    if (sameSource && GameSession->IsDragging())
      GameSession->CancelDrag();
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
        routeUp(GetPlayerWindowRoot());
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
        routeUp(GetPlayerWindowRoot());
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

  const bool wasCrafting = OverlayCaptures[pointerIndex] == OverlayPointerCapture::Crafting;
  const bool wasWorldGen = OverlayCaptures[pointerIndex] == OverlayPointerCapture::WorldGen;
  OverlayCaptures[pointerIndex] = OverlayPointerCapture::None;
  OverlayPressedWidget = nullptr;
  if (CraftingScreen && wasCrafting)
    CraftingScreen->SetPointerPressed(std::find(OverlayCaptures.begin(), OverlayCaptures.end(),
        OverlayPointerCapture::Crafting) != OverlayCaptures.end());
  if (WorldGenScreen && wasWorldGen)
    WorldGenScreen->SetPointerPressed(std::find(OverlayCaptures.begin(), OverlayCaptures.end(),
        OverlayPointerCapture::WorldGen) != OverlayCaptures.end());
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
    std::function<void(UGuiWidget *)> focusText = [&](UGuiWidget *node)
    {
      if (auto *input = dynamic_cast<UGuiTextInput *>(node))
        input->SetFocused(node == hit);
      for (const auto &child : node->GetChildren())
        focusText(child.get());
    };
    focusText(root);
    if (PlayerWindowScreen && root == PlayerWindowScreen->GetRoot())
    {
      focusText(CharacterSheetScreen->GetRoot());
      focusText(PaletteScreen->GetRoot());
    }
    if (auto *input = dynamic_cast<UGuiTextInput *>(hit))
    {
      return input->PointerDown(event, GuiContext->GetRenderer()) ? hit
                                                                  : nullptr;
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
    if (WorldGenOpen && WorldGenScreen)
    {
      if (UGuiWidget *hit = routeRootDown(WorldGenScreen->GetRoot()))
      {
        WorldGenScreen->SetPointerPressed(true);
        OverlayCaptures[pointerIndex] = OverlayPointerCapture::WorldGen;
        OverlayPressedWidget = hit;
        return true;
      }
    }
    if (PaletteOpen)
    {
      if (UGuiWidget *hit = routeRootDown(GetPlayerWindowRoot()))
      {
        OverlayCaptures[pointerIndex] = OverlayPointerCapture::Palette;
        OverlayPressedWidget = hit;
        return true;
      }
    }
    if (CraftingOpen && CraftingScreen)
    {
      if (UGuiWidget *hit = routeRootDown(CraftingScreen->GetRoot()))
      {
        CraftingScreen->SetPointerPressed(true);
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
      if (UGuiWidget *hit = routeRootDown(GetPlayerWindowRoot()))
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
    if (PaletteOpen || CharacterSheetOpen)
      return true;
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
  if ((PaletteOpen || CharacterSheetOpen) && PlayerWindowScreen &&
      PlayerWindowScreen->PickHotbarSlot(x, y, out))
    return true;

  // Hotbar targets first so Tools grid never swallows drops meant for hotbar.
  if (PaletteOpen && PaletteScreen && PaletteScreen->PickHotbarStrip(x, y, out))
  {
    return true;
  }
  if (HudScreen && !PaletteOpen && !CharacterSheetOpen &&
      HudScreen->PickSlot(x, y, out))
  {
    return true;
  }
  if (CharacterSheetOpen && CharacterSheetScreen)
  {
    size_t bagSlot = 0;
    if (CharacterSheetScreen->PickBackpackSlot(x, y, bagSlot))
    {
      out = {};
      out.surface = SlotSurface::Hotbar;
      out.bar = bagSlot / 10;
      out.slot = bagSlot % 10;
      return true;
    }
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
    if (CharacterSheetScreen->PickDoll(x, y))
    {
      out = SlotAddress{};
      out.surface = SlotSurface::CharacterDoll;
      return true;
    }
    if (CharacterSheetScreen->PickMainSlot(x, y) && GameSession)
    {
      out = SlotAddress{};
      out.surface = SlotSurface::Hotbar;
      out.bar = World && World->GetControlledCreature()
                    ? World->GetControlledCreature()
                          ->GetInventory()
                          .GetActiveBarIndex()
                    : 0;
      out.slot = GameSession->GetSelectedSlot(out.bar);
      return true;
    }
  }
  if (PaletteOpen && PaletteScreen && PaletteScreen->PickGridSlot(x, y, out))
  {
    return true;
  }
  return false;
}

static float FpItemLight(UWorld &world, const UCreature &actor)
{
  auto light = glm::vec2(0);
  const auto cell = WorldPosToBlock(actor.GetEyePosition());
  const auto sample =
      world.RequestSceneLightSample(cell, SceneLightConsumer::FirstPersonItem);
  if (sample.IsReady())
  {
    const auto packed = *sample.packed;
    light = glm::vec2(UnpackSky(packed), UnpackBlock(packed)) /
            float(kMaxLightLevel);
  }
  const auto &env = world.GetEnvironmentState();
  return ItemWorldLightIntensity(
      light, env.DayNightFactor, env.MoonNightFactor,
      SkyLightScaleFromWeather(env.WeatherSkyAttenuation,
                               env.PrecipitationIntensity),
      world.GetLightingSettings().MinAmbient);
}

static void ConfigureCarryContacts(FpViewmodelDrawParams &params, UWorld &world,
                                   const UCreature &actor,
                                   const UCamera &camera)
{
  glm::vec3 right, left;
  if (!ResolveCarryContacts(world, actor, right, left))
    return;
  const auto vp = camera.GetProjection() * camera.GetViewMatrix();
  const auto r = vp * glm::vec4(right, 1), l = vp * glm::vec4(left, 1);
  if (r.w <= .001f || l.w <= .001f)
    return;
  params.CarryRightNdc = glm::vec2(r) / r.w;
  params.CarryLeftNdc = glm::vec2(l) / l.w;
  params.HasCarryContacts =
      std::isfinite(params.CarryRightNdc.x) &&
      std::isfinite(params.CarryRightNdc.y) &&
      std::isfinite(params.CarryLeftNdc.x) &&
      std::isfinite(params.CarryLeftNdc.y) &&
      glm::all(
          glm::lessThanEqual(glm::abs(params.CarryRightNdc), glm::vec2(2))) &&
      glm::all(glm::lessThanEqual(glm::abs(params.CarryLeftNdc), glm::vec2(2)));
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
  const GLuint tex = IconSource->GetEntryIcon(drag.entry);
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
  if (World)
    if (auto *actor = World->GetControlledCreature())
      if (actor->GetTypeId() == "human")
        actor->HumanMotion.actionRemaining = .28f;
  if (FpViewmodelRenderer)
  {
    FpViewmodelRenderer->NotifySwing(kind);
  }
  // Melee swing whoosh right as the viewmodel starts moving (dig/place have
  // their own impact sounds at the block).
  if (kind == FpSwingKind::Melee && World)
  {
    if (const std::shared_ptr<UCamera> &camera = World->GetCurrentUserCamera())
    {
      const glm::vec3 eye = camera->GetPosition();
      UAudioEvent audio;
      audio.Kind = AudioEventKind::MeleeSwing;
      audio.X = eye.x;
      audio.Y = eye.y;
      audio.Z = eye.z;
      UAudioApi::Post(audio);
    }
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
    FpViewmodelRenderer->ClearHeldVisual();
}

void UApplication::EndFpUseVisual()
{
  if (FpViewmodelRenderer)
  {
    FpViewmodelRenderer->EndUseVisual();
  }
}

void UApplication::CancelOverlayCapture(OverlayPointerCapture capture)
{
  const auto captured = std::find(OverlayCaptures.begin(), OverlayCaptures.end(), capture);
  if (captured != OverlayCaptures.end())
  {
    if (auto *button = dynamic_cast<UGuiButton *>(OverlayPressedWidget))
      button->ClearPressState();
    if (auto *slot = dynamic_cast<UGuiSlot *>(OverlayPressedWidget))
      slot->ClearPressState();
    OverlayPressedWidget = nullptr;
    std::replace(captured, OverlayCaptures.end(), capture, OverlayPointerCapture::None);
  }
}

void UApplication::CloseCraftingScreen()
{
  CraftingOpen = false;
  CancelOverlayCapture(OverlayPointerCapture::Crafting);
  if (CraftingScreen)
    CraftingScreen->SetVisible(false);
}

void UApplication::CloseAnvilScreen()
{
  AnvilOpen = false;
  CancelOverlayCapture(OverlayPointerCapture::Anvil);
  if (AnvilScreen)
    AnvilScreen->SetVisible(false);
}

void UApplication::CloseWorldGenScreen()
{
  WorldGenOpen = false;
  CancelOverlayCapture(OverlayPointerCapture::WorldGen);
  if (WorldGenScreen)
  {
    WorldGenScreen->SetPointerPressed(false);
    WorldGenScreen->SetVisible(false);
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
  CloseAnvilScreen();
  PaletteOpen = false;
  CloseWorldGenScreen();
  CharacterSheetOpen = false;
  if (PaletteScreen)
  {
    PaletteScreen->SetVisible(false);
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
  CloseCraftingScreen();
  PaletteOpen = false;
  CloseWorldGenScreen();
  CharacterSheetOpen = false;
  if (PaletteScreen)
  {
    PaletteScreen->SetVisible(false);
  }
  if (CharacterSheetScreen)
  {
    CharacterSheetScreen->SetVisible(false);
  }
  AnvilScreen->SetVisible(true);
  SyncCursorVisibility();
}

void UApplication::RunOwnershipUiQa(const std::string &path)
{
  if (!World || !GameSession || !CharacterSheetScreen || !PaletteScreen)
    throw std::runtime_error("Ownership UI fixture unavailable");
  auto *actor = World->GetControlledCreature();
  auto &inv = actor->GetInventory();
  auto savedInv = inv;
  const auto savedMode = World->GetGameMode();
  auto &physics = UWorldItemSystem::Instance();
  const auto position = actor->GetBodyOrigin();
  const auto savedInventoryKey = Ui.InventoryKey;
  const auto savedPaletteKey = Ui.PaletteKey;
  Ui.InventoryKey = "e";
  Ui.PaletteKey = "b";
  nlohmann::json report = {{"checks", nlohmann::json::array()}};
  auto check = [&](const std::string &name, bool ok)
  { report["checks"].push_back({{"name", name}, {"passed", ok}}); };
  auto click = [&](GuiRect bounds, GuiMouseButton button = GuiMouseButton::Left)
  {
    const int x = bounds.X + bounds.W / 2, y = bounds.Y + bounds.H / 2;
    const int physical = button == GuiMouseButton::Right
                             ? GLFW_MOUSE_BUTTON_RIGHT
                             : GLFW_MOUSE_BUTTON_LEFT;
    InputRouter.RouteMouseButton(*this, physical, true, x, y, 0);
    InputRouter.RouteMouseButton(*this, physical, false, x, y, 0);
  };
  for (auto mode : {WorldGameMode::Creative, WorldGameMode::Survival})
  {
    physics.Clear();
    World->SetGameMode(mode);
    inv = UCreatureInventory{};
    inv.SetItemDefinitions(World->GetItemDefinitionStorage());
    inv.SetCreative(mode == WorldGameMode::Creative);
    GameSession->CancelDrag();
    PaletteOpen = false;
    CharacterSheetOpen = false;
    PaletteScreen->SetVisible(false);
    CharacterSheetScreen->SetVisible(false);
    InputRouter.RouteKey(*this, GLFW_KEY_E, GLFW_PRESS, 0);
    check(mode == WorldGameMode::Creative ? "E opens Creative catalog"
                                          : "E opens Survival backpack",
          mode == WorldGameMode::Creative ? PaletteOpen && !CharacterSheetOpen
                                          : CharacterSheetOpen && !PaletteOpen);
    if (mode == WorldGameMode::Creative)
    {
      for (int frame = 0; frame < 24; ++frame)
        PaletteScreen->Update(0);
      check("large Creative block catalog finishes progressive grid loading",
            PaletteScreen->IsGridComplete() &&
                PaletteScreen->GetGridEntryCount() > 256);
      IconSource->ClearBlockIconCache();
      PaletteScreen->InvalidateGrid();
      PaletteScreen->Update(0);
      const auto iconsBefore = PaletteScreen->GetGridIconCount();
      PaletteScreen->Update(0);
      check("cold catalog icons appear while the background queue is pending",
            PaletteScreen->GetGridIconCount() > iconsBefore &&
                IconSource->PendingBlockIcons() > 0);
      const int gridX = PaletteScreen->GetViewportWidth() * 2 / 5;
      const int gridY = PaletteScreen->GetViewportHeight() * 11 / 20;
      SlotAddress firstPage, nextPage, restoredPage;
      const bool firstPicked =
          PaletteScreen->PickGridSlot(gridX, gridY, firstPage);
      InputRouter.RouteScroll(*this, 0, -20, gridX, gridY);
      PaletteScreen->Update(0);
      const bool nextPicked =
          PaletteScreen->PickGridSlot(gridX, gridY, nextPage);
      InputRouter.RouteScroll(*this, 0, 20, gridX, gridY);
      PaletteScreen->Update(0);
      check("catalog scrolling exposes new slots and restores the first page",
            firstPicked && nextPicked &&
                firstPage.entryId != nextPage.entryId &&
                PaletteScreen->PickGridSlot(gridX, gridY, restoredPage) &&
                restoredPage.entryId == firstPage.entryId);
    }
    InputRouter.RouteKey(*this, GLFW_KEY_B, GLFW_PRESS, 0);
    check("B respects mode and keeps a single player window",
          mode == WorldGameMode::Creative ? PaletteOpen && !CharacterSheetOpen
                                          : !PaletteOpen && CharacterSheetOpen);
    Ui.InventoryKey = "i";
    PaletteOpen = false;
    CharacterSheetOpen = false;
    PaletteScreen->SetVisible(false);
    CharacterSheetScreen->SetVisible(false);
    InputRouter.RouteKey(*this, GLFW_KEY_I, GLFW_PRESS, 0);
    check("rebound inventory action works", PaletteOpen || CharacterSheetOpen);
    Ui.InventoryKey = "e";
    OpenPlayerWindow(1);
    InventoryEntryRef tool;
    tool.empty = false;
    tool.kind = InventoryEntryKind::Item;
    tool.Id = "iron_pickaxe";
    tool.count = 1;
    tool.wear = .53f;
    inv.Insert(tool);
    inv.Move(0, 21);
    inv.SetActiveSlot(2, 1);
    CharacterSheetScreen->Update(0);
    const auto identity = inv.GetSlot(21).instanceId;
    std::this_thread::sleep_for(std::chrono::milliseconds(320));
    click(CharacterSheetScreen->GetMainSlotBounds());
    check("full hand click retains worn instance and starts cursor transfer",
          inv.GetSlot(21).instanceId == identity &&
              inv.GetSlot(21).wear == .53f && GameSession->IsDragging() &&
              physics.Count() == 0);
    click(CharacterSheetScreen->GetDropButtonBounds());
    nlohmann::json bodies;
    physics.Serialize(bodies);
    bool found = false;
    for (const auto &r : bodies["world_physical_v2"])
      if (r["instance"].get<uint64_t>() == identity)
        found = true;
    check("Drop click transfers the actual hand instance into the world",
          inv.GetSlot(21).empty && found && !GameSession->IsDragging());
    for (int i = 0; i < 100; ++i)
      physics.Tick(*World, 1.f / 60);
    check("clicked Drop stays in world after pickup delay",
          inv.GetSlot(21).empty && physics.Count() == 1);
    if (mode == WorldGameMode::Creative)
    {
      auto ref = tool;
      ref.form = InventoryEntryForm::CatalogReference;
      ref.wear = 0;
      ref.instanceId = 0;
      inv.AssignToHotbar(2, 1, ref);
      CharacterSheetScreen->Update(0);
      const auto count = physics.Count();
      click(CharacterSheetScreen->GetDeleteButtonBounds());
      check("explicit delete removes only a catalog assignment",
            inv.GetSlot(21).empty && physics.Count() == count);
      inv.Insert(tool);
      inv.SetActiveSlot(0, 0);
      CharacterSheetScreen->Update(0);
      click(CharacterSheetScreen->GetDeleteButtonBounds());
      check("delete assignment cannot destroy a finite item",
            !inv.GetSlot(0).empty);
    }
  }
  physics.Clear();
  World->SetGameMode(WorldGameMode::Creative);
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(World->GetItemDefinitionStorage());
  inv.SetCreative(true);
  const auto camera = World->GetCurrentUserCamera();
  const auto node =
      WorldPosToBlock(camera->GetPosition() + camera->GetFront() * 2.f);
  const auto previous = World->GetBlockWorld().GetBlock(node);
  World->ReplaceBlockAt(node, "stone");
  PaletteOpen = false;
  CharacterSheetOpen = false;
  PaletteScreen->SetVisible(false);
  CharacterSheetScreen->SetVisible(false);
  InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_MIDDLE, true, 0, 0, 0);
  const auto *picked = inv.GetActiveEntryRef();
  check("middle button picks a catalog block without mining",
        picked && picked->IsCatalogReference() && picked->Id == "stone" &&
            World->GetBlockWorld().GetBlock(node) != BLOCK_AIR);
  World->GetBlockWorld().SetBlock(node, previous);
  physics.Clear();
  World->SetGameMode(savedMode);
  inv = savedInv;
  actor->SetBodyOrigin(position);
  Ui.InventoryKey = savedInventoryKey;
  Ui.PaletteKey = savedPaletteKey;
  GameSession->CancelDrag();
  OpenPlayerWindow(0);
  report["passed"] = true;
  for (const auto &c : report["checks"])
    if (!c["passed"].get<bool>())
      report["passed"] = false;
  std::ofstream out(path);
  out << report.dump(2) << '\n';
}

void UApplication::RunPlayerWindowQa(const std::string &path)
{
  const auto originalMode = World->GetGameMode();
  const auto originalScale = GuiContext->GetMetrics().Scale;
  const auto originalWidth = PlayerWindowScreen->GetViewportWidth();
  const auto originalHeight = PlayerWindowScreen->GetViewportHeight();
  auto originalInventory = World->GetControlledCreature()->GetInventory();
  auto &qaInventory = World->GetControlledCreature()->GetInventory();
  qaInventory = UCreatureInventory{};
  qaInventory.SetItemDefinitions(World->GetItemDefinitionStorage());
  qaInventory.SetCreative(true);
  InventoryEntryRef qaTool;
  qaTool.empty = false;
  qaTool.kind = InventoryEntryKind::Item;
  qaTool.Id = "iron_pickaxe";
  qaTool.count = 1;
  qaTool.wear = .27f;
  qaInventory.Insert(qaTool);
  auto qaBlock = qaTool;
  qaBlock.kind = InventoryEntryKind::Block;
  qaBlock.Id = "stone";
  qaBlock.count = 64;
  qaBlock.wear = 0;
  qaInventory.Insert(qaBlock);
  auto qaFood = qaTool;
  qaFood.Id = "apple";
  qaFood.count = 3;
  qaFood.wear = 0;
  qaInventory.Insert(qaFood);
  qaBlock.form = InventoryEntryForm::CatalogReference;
  qaBlock.count = 1;
  qaInventory.AssignToHotbar(3, 9, qaBlock);
  nlohmann::json report = {{"checks", nlohmann::json::array()},
                           {"layouts", nlohmann::json::array()}};
  auto equal = [](GuiRect a, GuiRect b)
  { return a.X == b.X && a.Y == b.Y && a.W == b.W && a.H == b.H; };
  auto check = [&](const std::string &name, bool ok)
  { report["checks"].push_back({{"name", name}, {"passed", ok}}); };
  auto click = [&](GuiRect b)
  {
    InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, true,
                                 b.X + b.W / 2, b.Y + b.H / 2, 0);
    InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, false,
                                 b.X + b.W / 2, b.Y + b.H / 2, 0);
  };
  for (auto mode : {WorldGameMode::Creative, WorldGameMode::Survival})
  {
    GameSession->SyncToWorldGameMode(mode);
    for (auto size : {std::pair<int, int>{1280, 720}, {1920, 1080}})
      for (float scale : {1.f, 1.5f, 2.f})
      {
        GuiContext->ApplyUiScale(scale);
        PlayerWindowScreen->OnMetricsChanged(GuiContext->GetMetrics());
        PlayerWindowScreen->OnViewportChanged(size.first, size.second);
        OpenPlayerWindow(0);
        PlayerWindowScreen->Update(0);
        const auto reference = PlayerWindowScreen->GetLayout();
        auto *root = GetPlayerWindowRoot();
        for (int section : {4, 0, 1, 2, 3, 0})
        {
          click(PlayerWindowScreen->GetTabBounds(section));
          if (CharacterSheetOpen)
            CharacterSheetScreen->Update(0);
          if (PaletteOpen)
            PaletteScreen->Update(0);
          PlayerWindowScreen->Update(0);
          const auto current = PlayerWindowScreen->GetLayout();
          const int expected =
              section == 4 && mode == WorldGameMode::Survival ? 0 : section;
          bool ok = GetPlayerWindowRoot() == root &&
                    PlayerWindowScreen->GetSection() == expected &&
                    equal(reference.Window, current.Window) &&
                    equal(reference.Tabs, current.Tabs) &&
                    equal(reference.Preview, current.Preview) &&
                    equal(reference.Content, current.Content) &&
                    equal(reference.Footer, current.Footer) &&
                    current.Window.X >= 0 && current.Window.Y >= 0 &&
                    current.Window.X + current.Window.W <= size.first &&
                    current.Window.Y + current.Window.H <= size.second;
          if (mode == WorldGameMode::Creative && (section == 0 || section == 4))
          {
            if (section == 4)
              PaletteScreen->RenderPreview();
            const auto imagePath =
                std::filesystem::path(path + ".frames") /
                (std::to_string(size.first) + "x" +
                 std::to_string(size.second) + "_" +
                 std::to_string(int(scale * 100)) +
                 (section == 4 ? "_catalog.png" : "_backpack.png"));
            ok = CapturePlayerWindow(*GuiContext, *root, size.first,
                                     size.second, imagePath) &&
                 ok;
          }
          report["layouts"].push_back(
              {{"mode",
                mode == WorldGameMode::Creative ? "creative" : "survival"},
               {"width", size.first},
               {"height", size.second},
               {"scale", scale},
               {"section", section},
               {"passed", ok},
               {"window",
                {current.Window.X, current.Window.Y, current.Window.W,
                 current.Window.H}}});
        }
      }
  }
  GameSession->SyncToWorldGameMode(WorldGameMode::Creative);
  GuiContext->ApplyUiScale(1.f);
  PlayerWindowScreen->OnMetricsChanged(GuiContext->GetMetrics());
  PlayerWindowScreen->OnViewportChanged(1280, 720);
  OpenPlayerWindow(0);
  CharacterSheetScreen->Update(0);
  qaInventory.SetActiveSlot(2, 3);
  CharacterSheetScreen->Update(0);
  PlayerWindowScreen->Update(0);
  const auto toolIdentity = qaInventory.GetSlot(0).instanceId;
  const auto from = CharacterSheetScreen->GetBackpackSlotBounds(0);
  const auto to = PlayerWindowScreen->GetHotbarBounds(3);
  InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, true,
                               from.X + from.W / 2, from.Y + from.H / 2, 0);
  InputRouter.RouteMouseMove(*this, to.X + to.W / 2, to.Y + to.H / 2, 0);
  InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, false,
                               to.X + to.W / 2, to.Y + to.H / 2, 0);
  check("shared footer transfers worn instances into the active inventory row",
        qaInventory.GetSlot(0).empty &&
            qaInventory.GetSlot(23).instanceId == toolIdentity &&
            qaInventory.GetSlot(23).wear == .27f);
  click(PlayerWindowScreen->GetHotbarBounds(4));
  check("shared footer selects within the active inventory row",
        qaInventory.GetActiveBarIndex() == 2 &&
            qaInventory.GetActiveSlotIndex() == 4);
  click(CharacterSheetScreen->GetSearchBounds());
  for (char c : std::string("stone"))
    InputRouter.RouteChar(*this, unsigned(c));
  const auto backpackState = CharacterSheetScreen->GetSectionStateKey();
  click(PlayerWindowScreen->GetTabBounds(4));
  PaletteScreen->OpenWithMainTab(0);
  for (int i = 0; i < 4; ++i)
    PaletteScreen->Update(0);
  const auto catalogArea = PlayerWindowScreen->GetLayout().Content;
  const auto unscrolled = PaletteScreen->GetSectionStateKey();
  InputRouter.RouteScroll(*this, 0, -5, catalogArea.X + catalogArea.W / 2,
                          catalogArea.Y + catalogArea.H / 2);
  PaletteScreen->Update(0);
  const auto scrolled = PaletteScreen->GetSectionStateKey();
  click(PlayerWindowScreen->GetTabBounds(0));
  click(PlayerWindowScreen->GetTabBounds(4));
  PaletteScreen->Update(0);
  check("nonzero catalog scroll survives backpack roundtrip",
        scrolled != unscrolled &&
            PaletteScreen->GetSectionStateKey() == scrolled);
  PaletteScreen->OpenWithMainTab(4);
  for (int i = 0; i < 4; ++i)
    PaletteScreen->Update(0);
  const auto gridArea = PlayerWindowScreen->GetLayout().Content;
  const auto &theme = GuiContext->GetTheme();
  const int gridY = gridArea.Y + theme.Padding * 4 + theme.TabBarHeight * 2 +
                    theme.FontSizeBody + 4 + theme.Padding / 2;
  click({gridArea.X + theme.Padding, gridY, theme.HotbarSlotSize,
         theme.HotbarSlotSize});
  PaletteScreen->Update(0);
  const auto beforeRotation = PaletteScreen->GetSectionStateKey();
  const auto previewBounds = PaletteScreen->GetPreviewBounds();
  const int previewX = previewBounds.X + previewBounds.W / 2;
  const int previewY = previewBounds.Y + previewBounds.H / 2;
  InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, true, previewX,
                               previewY, 0);
  InputRouter.RouteMouseMove(*this, previewX + 30, previewY + 12, 0);
  InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, false,
                               previewX + 30, previewY + 12, 0);
  check("catalog selection and preview rotation use the shared input tree",
        PaletteScreen->GetSectionStateKey() != beforeRotation);
  const auto catalogState = PaletteScreen->GetSectionStateKey();
  click(PlayerWindowScreen->GetTabBounds(0));
  CharacterSheetScreen->Update(0);
  check("backpack search and section state survive catalog roundtrip",
        CharacterSheetScreen->GetSectionStateKey() == backpackState);
  click(PlayerWindowScreen->GetTabBounds(4));
  PaletteScreen->Update(0);
  check("catalog category selection and scroll survive backpack roundtrip",
        PaletteScreen->GetSectionStateKey() == catalogState &&
            PaletteScreen->GetActiveMainTab() == 4);
  click(PlayerWindowScreen->GetTabBounds(3));
  CharacterSheetScreen->Update(0);
  const auto area = PlayerWindowScreen->GetLayout().Content;
  const int pad = GuiContext->GetMetrics().Dp(8),
            line = GuiContext->GetMetrics().Dp(24);
  click({area.X + pad, area.Y + pad, area.W - pad * 2, line + pad / 2});
  CharacterSheetScreen->Update(0);
  const auto appearanceState = CharacterSheetScreen->GetSectionStateKey();
  click(PlayerWindowScreen->GetTabBounds(4));
  click(PlayerWindowScreen->GetTabBounds(3));
  CharacterSheetScreen->Update(0);
  check("unapplied appearance draft survives catalog roundtrip",
        CharacterSheetScreen->GetSectionStateKey() == appearanceState);
  // Cancel the draft through the same button as the player.
  click({area.X + pad, area.Y + pad + 5 * (line + pad), area.W - pad * 2,
         line + pad / 2});
  OpenPlayerWindow(0);
  click(CharacterSheetScreen->GetSearchBounds());
  InputRouter.RouteKey(*this, GLFW_KEY_A, GLFW_PRESS, GLFW_MOD_CONTROL);
  InputRouter.RouteKey(*this, GLFW_KEY_BACKSPACE, GLFW_PRESS, 0);
  if (std::getenv("CUBA_PALETTE_GRID_QA"))
  {
    GameSession->SyncToWorldGameMode(WorldGameMode::Creative);
    OpenPlayerWindow(4);
    PaletteScreen->OpenWithMainTab(0);
    IconSource->ClearBlockIconCache();
    PaletteScreen->Update(0);
    const auto first = InspectPaletteGridStorage();
    const auto beforeIcons = PaletteScreen->GetGridIconCount();
    PaletteScreen->Update(0);
    const auto next = InspectPaletteGridStorage();
    const auto afterIcons = PaletteScreen->GetGridIconCount();
    const auto pending = IconSource->PendingBlockIcons();
    for (int frame = 0; frame < 24 && !PaletteScreen->IsGridComplete(); ++frame)
      PaletteScreen->Update(0);
    const auto complete = InspectPaletteGridStorage();
    auto row = [](const PaletteGridStorage &storage) {
      return nlohmann::json{{"supported", storage.supported}, {"rows", storage.rows},
                            {"widgets", storage.widgets}, {"complete", storage.complete},
                            {"callbacks", storage.callbacks}, {"callback_bytes", storage.callback_bytes},
                            {"requested", storage.requested}};
    };
    report["palette_grid"] = {{"first", row(first)}, {"next", row(next)}, {"complete", row(complete)},
        {"before_icons", beforeIcons}, {"after_icons", afterIcons}, {"pending_icons", pending}};
    check("catalog publishes complete bounded batches with shared rows",
          first.supported && next.supported && complete.supported && first.widgets == 256 &&
              first.rows > first.widgets && next.widgets > first.widgets &&
              next.widgets <= first.widgets + 256 && complete.complete &&
              complete.rows == complete.widgets && complete.callbacks == complete.widgets * 2 &&
              !complete.callback_bytes);
    check("visible catalog icons progress while background work remains queued",
          afterIcons > beforeIcons && pending > 0);
    const int gridX = PaletteScreen->GetViewportWidth() * 2 / 5;
    const int gridY = PaletteScreen->GetViewportHeight() * 11 / 20;
    SlotAddress held;
    const bool picked = PaletteScreen->PickGridSlot(gridX, gridY, held);
    const bool pressed = InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, true, gridX, gridY, 0);
    const bool captured = HasAnyOverlayCapture();
    EnterInGameInputState();
    const bool released = !HasAnyOverlayCapture() && !OverlayPressedWidget;
    OpenPlayerWindow(4);
    PaletteScreen->OpenWithMainTab(4);
    PaletteScreen->Update(0);
    const auto afterReset = InspectPaletteGridStorage();
    InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, false, gridX, gridY, 0);
    check("resetting gameplay input retires the held catalog slot before publishing another tab",
          picked && pressed && captured && released && afterReset.supported &&
              afterReset.current && afterReset.kind == ContentKind::Item);
  }
  GameSession->CancelDrag();
  GameSession->SyncToWorldGameMode(originalMode);
  World->GetControlledCreature()->GetInventory() = originalInventory;
  GuiContext->ApplyUiScale(originalScale);
  PlayerWindowScreen->OnMetricsChanged(GuiContext->GetMetrics());
  PlayerWindowScreen->OnViewportChanged(originalWidth, originalHeight);
  OpenPlayerWindow(0);
  report["passed"] = true;
  for (const auto *rows : {"checks", "layouts"})
    for (const auto &entry : report[rows])
      if (!entry["passed"].get<bool>())
        report["passed"] = false;
  std::ofstream out(path);
  out << report.dump(2) << '\n';
}
void UApplication::ToggleItemLab()
{
  if (State != AppState::InGame || !GuiContext || !World)
    return;
  if (ItemLabOpen)
  {
    ItemLabOpen = false;
    ItemLabScreen = nullptr;
    GuiContext->SetScreen(nullptr);
    SyncCursorVisibility();
    return;
  }
  if (DeathScreenOpen || World->IsPlayerDead())
    return;
  if (GameSession)
    GameSession->CancelDrag();
  EnterInGameInputState();
  if (auto *actor = World->GetControlledCreature())
    UWorldItemSystem::Instance().CancelCharge(*actor);
  auto screen = std::make_unique<UItemLabScreen>(
      Core->GetItemDefinitionStorage(), ShaderManager,
      std::make_unique<UItemLabSceneRenderer>(
          Core->GetItemDefinitionStorage(), BlockDefinitions,
          Core->GetTextureCubeStorage(), Core->GetCreatureTextureStorage(),
          World->GetCreatureDefinitionStorage(),
          World->GetSkinDefinitionStorage(), ShaderManager));
  ItemLabScreen = screen.get();
  GuiContext->SetScreen(std::move(screen));
  ItemLabOpen = true;
  SyncCursorVisibility();
}

void UApplication::UpdateItemLab(double dt)
{
  if (ItemLabOpen && GuiContext->GetScreen() != ItemLabScreen)
  {
    ItemLabOpen = false;
    ItemLabScreen = nullptr;
  }
  if (ItemLabOpen)
  {
    GuiContext->Update(dt);
    if (ItemLabScreen->WantsClose())
      ToggleItemLab();
  }
}

void UApplication::RunItemLabQa(const std::string &path)
{
  nlohmann::json before;
  World->GetControlledCreature()->GetInventory().SerializeToJson(before);
  InputRouter.RouteKey(*this, GLFW_KEY_F8, GLFW_PRESS, GLFW_MOD_CONTROL);
  if (!ItemLabScreen)
    throw std::runtime_error("Item Lab did not open");
  nlohmann::json report = {{"passed", true},
                           {"checks", nlohmann::json::array()}};
  auto check = [&](const std::string &name, bool ok)
  {
    report["checks"].push_back({{"name", name}, {"passed", ok}});
    if (!ok)
      report["passed"] = false;
  };
  int displayWidth = 0, displayHeight = 0;
  glfwGetFramebufferSize(Window, &displayWidth, &displayHeight);
  ItemLabScreen->OnViewportChanged(displayWidth, displayHeight);
  ItemLabScreen->Update(0);
  report["display"] = {{"width", displayWidth},
                       {"height", displayHeight},
                       {"scale", Ui.UiScaleUser},
                       {"effective_scale", GetUiScale()}};
  check("catalog includes every item",
        ItemLabScreen->GetItemCount() ==
            Core->GetItemDefinitionStorage()->ListCatalogIds().size());
  for (const auto &id : Core->GetItemDefinitionStorage()->ListCatalogIds())
  {
    ItemLabScreen->SelectItem(id);
    ItemLabScreen->Update(0);
    check("published preview " + id, ItemLabScreen->GetPreviewTexture(0) != 0);
  }
  ItemLabScreen->SelectItem("bucket");
  ItemLabScreen->Update(0);
  const auto bucketModel =
      IUPlatformPaths::Current().AssetRoot() / "models/items/bucket/model.gltf";
  const auto archive =
      bucketModel.parent_path() / "_itemgen_reference/model.gltf";
  check("archived reference selected when available",
        ItemLabScreen->UsesArchivedReference() ==
            std::filesystem::is_regular_file(archive));
  if (ItemLabScreen->UsesArchivedReference())
  {
    check("reference differs from current candidate",
          ItemLabScreen->GetReferencePath() !=
              ItemLabScreen->GetCandidatePath());
    report["archived_reference"] =
        ItemLabScreen->GetReferencePath().generic_string();
    report["candidate"] = ItemLabScreen->GetCandidatePath().generic_string();
    report["staged_candidate"] = ItemLabScreen->UsesStagedCandidate();
  }
  for (int mode = 0; mode < 7; ++mode)
  {
    ItemLabScreen->SetMode(mode);
    ItemLabScreen->Update(0);
    check("candidate inspection mode " + std::to_string(mode),
          ItemLabScreen->HasCandidate());
    check("capture inspection mode " + std::to_string(mode),
          CapturePlayerWindow(*GuiContext, *ItemLabScreen->GetRoot(),
                              ItemLabScreen->GetViewportWidth(),
                              ItemLabScreen->GetViewportHeight(),
                              std::filesystem::path(path + ".frames") /
                                  ("mode_" + std::to_string(mode) + ".png")));
  }
  auto click = [&](const GuiRect &b)
  {
    for (bool pressed : {true, false})
      InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, pressed,
                                   b.X + b.W / 2, b.Y + b.H / 2, 0);
  };
  click(ItemLabScreen->GetStateControlBounds(2));
  ItemLabScreen->Update(0);
  check("independent fill click does not activate use",
        ItemLabScreen->VisualInputs(1).HasWater &&
            ItemLabScreen->VisualInputs(1).UsePhase == 0 &&
            ItemLabScreen->GetPosePreset() == -1);
  click(ItemLabScreen->GetStateControlBounds(1));
  ItemLabScreen->Update(0);
  check("unsupported bucket use control cannot change state",
        ItemLabScreen->VisualInputs(1).UsePhase == 0);
  ItemLabScreen->SelectItem("waterskin");
  ItemLabScreen->Update(0);
  click(ItemLabScreen->GetStateControlBounds(1));
  ItemLabScreen->Update(0);
  check("independent use click preserves fill",
        ItemLabScreen->VisualInputs(1).HasWater &&
            ItemLabScreen->VisualInputs(1).UsePhase == .25f);
  click(ItemLabScreen->GetMotionBounds());
  click(ItemLabScreen->GetMotionBounds());
  ItemLabScreen->Update(.5);
  check("playing character motion does not animate item state",
        ItemLabScreen->VisualInputs(1).HasWater &&
            ItemLabScreen->VisualInputs(1).UsePhase == .25f);
  click(ItemLabScreen->GetMotionBounds());
  ItemLabScreen->SelectItem("bucket");
  ItemLabScreen->Update(0);
  const auto exports =
      IUPlatformPaths::Current().AssetRoot() / "item_lab_exports";
  const auto exportReport = exports / "bucket-6.json";
  std::filesystem::remove(exportReport);
  for (const auto &suffix : {"-reference.png", "-candidate.png"})
    std::filesystem::remove(exports / (std::string("bucket-6") + suffix));
  click(ItemLabScreen->GetExportBounds());
  check("export writes fresh report",
        std::filesystem::is_regular_file(exportReport));
  if (std::filesystem::is_regular_file(exportReport))
  {
    std::ifstream file(exportReport);
    const auto exported = nlohmann::json::parse(file);
    check("export includes exact resource costs and authoritative contacts",
          exported.value("schema_version", 0) == 5 &&
              exported.at("pose") == "Custom" &&
              exported.at("state_controls").at("fill") == 1.f &&
              exported.at("state_controls").at("use") == .25f &&
              exported.at("panels").size() == 2 &&
              exported.at("panels")[1]
                      .at("geometry_buffer_bytes")
                      .get<uint64_t>() > 0 &&
              exported.at("panels")[1].at("rgba8_mip_bytes").get<uint64_t>() >
                  0 &&
              exported.at("panels")[1].at("contacts").contains("right") &&
              exported.at("panels")[1].at("physical").contains("size"));
  }
  for (const auto &suffix : {"-reference.png", "-candidate.png"})
    check(std::string("export image ") + suffix,
          std::filesystem::is_regular_file(exports /
                                           (std::string("bucket-6") + suffix)));
  const auto revision = UItemDefinitionStorage::ContentRevision();
  click(ItemLabScreen->GetReloadBounds());
  ItemLabScreen->Update(0);
  check("reload invalidates gameplay visual caches",
        UItemDefinitionStorage::ContentRevision() > revision &&
            ItemLabScreen->HasCandidate());
  check("reload preserves independent custom container state",
        ItemLabScreen->VisualInputs(1).HasWater &&
            ItemLabScreen->VisualInputs(1).UsePhase == .25f &&
            ItemLabScreen->GetPosePreset() == -1);
  ItemLabScreen->SetPosePreset(0);
  ItemLabScreen->SetMode(0);
  for (int pose = 0; pose < 3; ++pose)
  {
    ItemLabScreen->SetPosePreset(pose);
    ItemLabScreen->Update(0);
    check("state preview " + std::to_string(pose),
          ItemLabScreen->HasCandidate());
    check("state capture " + std::to_string(pose),
          CapturePlayerWindow(*GuiContext, *ItemLabScreen->GetRoot(),
                              ItemLabScreen->GetViewportWidth(),
                              ItemLabScreen->GetViewportHeight(),
                              std::filesystem::path(path + ".frames") /
                                  ("pose_" + std::to_string(pose) + ".png")));
  }
  RunItemLabSceneQa(*ItemLabScreen, *GuiContext,
                    std::filesystem::path(path + ".frames"), report);
  RunItemLabDiagnosticsQa(
      *ItemLabScreen, *GuiContext,
      Core->GetItemDefinitionStorage()->ListCatalogIds(),
      std::filesystem::path(path + ".frames") / "diagnostics", report);
  if (const char *art = std::getenv("CUBA_ITEM_LAB_ART_QA");
      art && std::string(art) == "1")
    RunItemLabArtQa(*ItemLabScreen,
                    Core->GetItemDefinitionStorage()->ListCatalogIds(),
                    std::filesystem::path(path + ".frames") / "art", report);
  ItemLabScreen->SetPosePreset(0);
  click(ItemLabScreen->GetPoseBounds());
  check("pointer switches state preset", ItemLabScreen->GetPosePreset() == 1);
  ItemLabScreen->SetPosePreset(0);
  const float scale = GuiContext->GetUiScale();
  const int originalWidth = ItemLabScreen->GetViewportWidth();
  const int originalHeight = ItemLabScreen->GetViewportHeight();
  for (auto size : {glm::ivec2(1280, 720), glm::ivec2(1920, 1080)})
    for (float uiScale : {1.f, 1.5f, 2.f})
    {
      GuiContext->ApplyUiScale(uiScale);
      ItemLabScreen->OnViewportChanged(size.x, size.y);
      ItemLabScreen->Update(0);
      const std::string label = std::to_string(size.x) + "x" +
                                std::to_string(size.y) + "_" +
                                std::to_string(int(uiScale * 100));
      check("layout " + label, ItemLabScreen->LayoutValid());
      check("capture " + label,
            CapturePlayerWindow(
                *GuiContext, *ItemLabScreen->GetRoot(), size.x, size.y,
                std::filesystem::path(path + ".frames") / (label + ".png")));
    }
  GuiContext->ApplyUiScale(scale);
  ItemLabScreen->OnViewportChanged(originalWidth, originalHeight);
  const auto button = ItemLabScreen->GetModeBounds();
  InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, true,
                               button.X + button.W / 2, button.Y + button.H / 2,
                               0);
  InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, false,
                               button.X + button.W / 2, button.Y + button.H / 2,
                               0);
  check("pointer switches inspection mode", ItemLabScreen->GetMode() == 1);
  InputRouter.RouteKey(*this, GLFW_KEY_ESCAPE, GLFW_PRESS, 0);
  check("Escape closes studio",
        !ItemLabOpen && GuiContext->GetScreen() == nullptr);
  nlohmann::json after;
  World->GetControlledCreature()->GetInventory().SerializeToJson(after);
  check("studio preserves player property", before == after);
  std::ofstream out(path);
  out << report.dump(2) << '\n';
}

void UApplication::RunInventoryIconsQa(const std::string &path)
{
  const auto originalMode = World->GetGameMode();
  auto &inventory = World->GetControlledCreature()->GetInventory();
  const auto originalInventory = inventory;
  const auto originalScale = GuiContext->GetMetrics().Scale;
  const auto originalWidth = PlayerWindowScreen->GetViewportWidth();
  const auto originalHeight = PlayerWindowScreen->GetViewportHeight();
  GameSession->SyncToWorldGameMode(WorldGameMode::Creative);
  GuiContext->ApplyUiScale(1.f);
  PlayerWindowScreen->OnMetricsChanged(GuiContext->GetMetrics());
  PlayerWindowScreen->OnViewportChanged(1280, 720);
  OpenPlayerWindow(0);
  nlohmann::json report = {{"checks", nlohmann::json::array()},
                           {"counts", nlohmann::json::object()}};
  auto expectedIcon = [&](InventoryEntryKind kind,
                          const std::string &id) -> unsigned
  {
    switch (kind)
    {
    case InventoryEntryKind::Block:
      return IconSource->GetBlockIconTexture(id);
    case InventoryEntryKind::Item:
      return IconSource->GetItemIconTexture(id);
    case InventoryEntryKind::Object:
      return IconSource->GetObjectIconTexture(id);
    case InventoryEntryKind::UCreature:
      return IconSource->GetCreatureIconTexture(id);
    case InventoryEntryKind::Skin:
      return IconSource->GetSkinIconTexture(id);
    }
    return 0;
  };
  auto reset = [&]
  {
    inventory = UCreatureInventory{};
    inventory.SetItemDefinitions(World->GetItemDefinitionStorage());
    inventory.SetCreative(true);
    inventory.SetActiveSlot(0, 0);
  };
  auto entry = [](InventoryEntryKind kind, const std::string &id)
  {
    InventoryEntryRef e;
    e.empty = false;
    e.kind = kind;
    e.Id = id;
    e.count = 1;
    e.form = InventoryEntryForm::CatalogReference;
    return e;
  };
  auto verify = [&](InventoryEntryKind kind, const std::string &id, size_t slot,
                    const std::string &location)
  {
    auto expected = expectedIcon(kind, id);
    IconSource->WarmupItemIcons(8);
    if (!expected)
      expected = expectedIcon(kind, id);
    CharacterSheetScreen->Update(0);
    PlayerWindowScreen->Update(0);
    const auto actual = CharacterSheetScreen->GetBackpackIconTexture(slot);
    const auto footer = PlayerWindowScreen->GetHotbarIconTexture(
        inventory.GetActiveSlotIndex());
    const bool ok = expected != 0 && actual == expected &&
                    (location == "recovery" || footer == expected);
    report["checks"].push_back({{"kind", int(kind)},
                                {"id", id},
                                {"location", location},
                                {"expected", expected},
                                {"backpack", actual},
                                {"footer", footer},
                                {"passed", ok}});
    return ok;
  };
  const auto &catalog = GameSession->GetContentCatalog();
  for (auto kind : {ContentKind::Object, ContentKind::UCreature,
                    ContentKind::Skin, ContentKind::Item})
  {
    const auto inventoryKind =
        kind == ContentKind::Object      ? InventoryEntryKind::Object
        : kind == ContentKind::UCreature ? InventoryEntryKind::UCreature
        : kind == ContentKind::Item      ? InventoryEntryKind::Item
                                         : InventoryEntryKind::Skin;
    const std::string label = kind == ContentKind::Object      ? "objects"
                              : kind == ContentKind::UCreature ? "creatures"
                              : kind == ContentKind::Item      ? "items"
                                                               : "skins";
    std::set<std::string> ids;
    for (const auto &type : catalog.GetTypeIds(kind))
      for (const auto &e : catalog.GetEntries(kind, type))
        ids.insert(e.Id);
    report["counts"][label] = ids.size();
    if (kind == ContentKind::Item)
    {
      const auto expected = World->GetItemDefinitionStorage()->ListCatalogIds();
      report["items_catalog_complete"] =
          (ids == std::set<std::string>(expected.begin(), expected.end()));
    }
    size_t index = 0;
    for (const auto &id : ids)
    {
      const size_t slot = index % 40;
      if (slot == 0)
        reset();
      inventory.AssignToHotbar(slot / 10, slot % 10, entry(inventoryKind, id));
      inventory.SetActiveSlot(slot / 10, slot % 10);
      verify(inventoryKind, id, slot, "backpack");
      ++index;
      if (index % 40 == 0 || index == ids.size())
      {
        const auto imagePath =
            std::filesystem::path(path + ".frames") /
            (label + "_" + std::to_string((index - 1) / 40) + ".png");
        if (!CapturePlayerWindow(*GuiContext, *GetPlayerWindowRoot(), 1280, 720,
                                 imagePath))
          throw std::runtime_error("Cannot capture inventory icon page");
      }
    }
  }
  reset();
  for (const char *id : {"bucket", "waterskin"})
  {
    reset();
    const auto *def = World->GetItemDefinitionStorage()->Get(id);
    const auto asset =
        def ? FindItemSurfaceAsset(IUPlatformPaths::Current().AssetRoot() /
                                       def->ModelPath,
                                   UItemDefinitionStorage::ContentRevision())
            : nullptr;
    int maximum = 0;
    if (asset)
      for (const auto &binding : asset->Bindings)
        if (binding.Channel == ItemVisualChannel::FillUnits)
          maximum = std::max(maximum, int(std::ceil(binding.Maximum)));
    report["container_state_support"][id] = maximum > 0;
    if (std::string(id) == "bucket")
      report["container_state_supported"] = maximum > 0;
    if (maximum <= 0)
      continue;
    auto container = entry(InventoryEntryKind::Item, id);
    container.form = InventoryEntryForm::FiniteStack;
    inventory.AssignToHotbar(0, 0, container);
    inventory.SetActiveSlot(0, 0);
    unsigned restIcon = 0;
    std::set<unsigned> filledIcons;
    int step = 0;
    const std::vector<int> sequence =
        maximum > 1 ? std::vector<int>{0, maximum / 2, maximum, 0}
                    : std::vector<int>{0, maximum, 0};
    for (int units : sequence)
    {
      auto *current = inventory.GetSlotMutable(0);
      current->contentsUnits = units;
      current->contentsId = units ? "water" : "";
      auto expected = IconSource->GetEntryIcon(*current);
      IconSource->WarmupItemIcons(8);
      expected = IconSource->GetEntryIcon(*current);
      CharacterSheetScreen->Update(0);
      PlayerWindowScreen->Update(0);
      const auto actual = CharacterSheetScreen->GetBackpackIconTexture(0);
      const auto footer = PlayerWindowScreen->GetHotbarIconTexture(0);
      if (step == 0)
        restIcon = expected;
      const bool correctState =
          units == 0
              ? expected == restIcon
              : expected != restIcon && filledIcons.insert(expected).second;
      report["checks"].push_back(
          {{"id", id},
           {"location", "contents-state"},
           {"units", units},
           {"expected", expected},
           {"backpack", actual},
           {"footer", footer},
           {"passed", expected != 0 && actual == expected &&
                          footer == expected && correctState}});
      if (!CapturePlayerWindow(
              *GuiContext, *GetPlayerWindowRoot(), 1280, 720,
              std::filesystem::path(path + ".frames") /
                  (std::string(id) + "_fill_" + std::to_string(step) + ".png")))
        throw std::runtime_error("Cannot capture stateful inventory icon");
      ++step;
    }
  }
  reset();
  auto sample = [&](ContentKind kind) -> std::string
  {
    for (const auto &type : catalog.GetTypeIds(kind))
    {
      const auto entries = catalog.GetEntries(kind, type);
      if (!entries.empty())
        return entries.front().Id;
    }
    throw std::runtime_error(
        "Inventory icon QA requires a catalog sample for kind " +
        std::to_string(int(kind)));
  };
  const std::vector<InventoryEntryRef> mixed = {
      entry(InventoryEntryKind::Block, "stone"),
      entry(InventoryEntryKind::Item, "iron_pickaxe"),
      entry(InventoryEntryKind::UCreature, "dolphin"),
      entry(InventoryEntryKind::Object, sample(ContentKind::Object)),
      entry(InventoryEntryKind::Skin, sample(ContentKind::Skin)),
      entry(InventoryEntryKind::Item, "bucket")};
  for (size_t i = 0; i < mixed.size(); ++i)
  {
    inventory.AssignToHotbar(0, i, mixed[i]);
    inventory.SetActiveSlot(0, i);
    verify(mixed[i].kind, mixed[i].Id, i, "mixed");
  }
  if (!CapturePlayerWindow(*GuiContext, *GetPlayerWindowRoot(), 1280, 720,
                           std::filesystem::path(path + ".frames") /
                               "mixed_dolphin.png"))
    throw std::runtime_error("Cannot capture mixed inventory");
  nlohmann::json state;
  inventory.SerializeToJson(state);
  state["creative_recovery"] = nlohmann::json::array();
  for (size_t i = 0; i < mixed.size(); ++i)
    state["creative_recovery"].push_back(state["creative_slots"][i]);
  inventory.DeserializeFromJson(state);
  CharacterSheetScreen->Update(0);
  const auto recovery = CharacterSheetScreen->GetRecoveryButtonBounds();
  auto clickRecovery = [&]
  {
    InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, true,
                                 recovery.X + recovery.W / 2,
                                 recovery.Y + recovery.H / 2, 0);
    InputRouter.RouteMouseButton(*this, GLFW_MOUSE_BUTTON_LEFT, false,
                                 recovery.X + recovery.W / 2,
                                 recovery.Y + recovery.H / 2, 0);
  };
  clickRecovery();
  for (size_t i = 0; i < mixed.size(); ++i)
    verify(mixed[i].kind, mixed[i].Id, i, "recovery");
  if (!CapturePlayerWindow(*GuiContext, *GetPlayerWindowRoot(), 1280, 720,
                           std::filesystem::path(path + ".frames") /
                               "recovery.png"))
    throw std::runtime_error("Cannot capture inventory recovery");
  clickRecovery();
  GameSession->SyncToWorldGameMode(originalMode);
  inventory = originalInventory;
  GuiContext->ApplyUiScale(originalScale);
  PlayerWindowScreen->OnMetricsChanged(GuiContext->GetMetrics());
  PlayerWindowScreen->OnViewportChanged(originalWidth, originalHeight);
  OpenPlayerWindow(0);
  report["passed"] = report.value("items_catalog_complete", false);
  for (const auto &row : report["checks"])
    if (!row["passed"].get<bool>())
      report["passed"] = false;
  std::ofstream out(path);
  out << report.dump(2) << '\n';
}
void UApplication::UpdateNativeItemsQa(double dt)
{
  if (World && World->GetControlledCreature())
    if (const char *path = std::getenv("CUBA_ITEM_WORLD_VISUAL_QA"))
    {
      if (!ItemWorldVisualQa)
      {
        std::ifstream input(std::filesystem::path(path).parent_path() /
                            "request.json");
        nlohmann::json request;
        input >> request;
        ItemWorldVisualQa = std::make_unique<UItemWorldVisualQa>(*World, path);
        glfwSetWindowSize(Window, request.at("width"), request.at("height"));
        ApplyLiveUiScale(request.at("scale").get<float>());
        Ui.ShowPerformance = false;
        if (Geometry)
          Geometry->SetShowPerformance(false);
        MinimalOverlayForBench = false;
      }
      ItemWorldVisualQa->Update(*World);
    }

  if (World && World->GetControlledCreature() && IconSource)
    if (const char *path = std::getenv("CUBA_ITEM_PERF_QA"))
    {
      if (!ItemPerformanceQa)
      {
        const char *scene = std::getenv("CUBA_ITEM_PERF_SCENE");
        ItemPerformanceQa = std::make_unique<UItemPerformanceQa>(
            *World, path, scene ? scene : "drops");
        glfwSetWindowSize(Window, 1920, 1080);
        Ui.UiScaleUser = 1.f;
        MinimalOverlayForBench =
            !ItemPerformanceQa->IsCatalog() && !ItemPerformanceQa->IsFp();
      }
      ItemPerformanceQa->Update(*World);
      if (ItemPerformanceQa->NeedsCatalog())
      {
        UItemDefinitionStorage::NotifyVisualAssetsChanged();
        ClearItemSurfaceAssets();
        ItemGltfTextureCache::Instance().Clear();
        IconSource->ClearItemIconCache();
        ItemPerformanceQa->BeginCatalog();
        for (const auto &id : ItemPerformanceQa->GetIds())
          IconSource->GetItemIconTexture(id);
        OpenPlayerWindow(4);
        PaletteScreen->OpenWithMainTab(4);
      }
      if (ItemPerformanceQa->CatalogActive())
      {
        IconSource->WarmupItemIcons(2);
        bool ready = true;
        for (const auto &id : ItemPerformanceQa->GetIds())
          ready = IconSource->GetItemIconTexture(id) != 0 && ready;
        ItemPerformanceQa->SetCatalogReady(ready &&
                                           !IconSource->PendingItemIcons());
      }
    }
  static bool itemPresentationDone = false;
  if (!itemPresentationDone && World && World->GetControlledCreature() &&
      CreaturePreviewRenderer)
    if (const char *path = std::getenv("CUBA_ITEM_PRESENTATION_QA"))
    {
      itemPresentationDone = true;
      nlohmann::json before, after;
      World->GetControlledCreature()->GetInventory().SerializeToJson(before);
      RunItemPresentationQa(path, Core->GetItemDefinitionStorage(),
                            *CreaturePreviewRenderer, ShaderManager,
                            FpViewmodelRenderer.get());
      World->GetControlledCreature()->GetInventory().SerializeToJson(after);
      std::ifstream input(path);
      nlohmann::json report;
      input >> report;
      input.close();
      report["inventory_unchanged"] = before == after;
      report["passed"] = report.value("passed", false) && before == after;
      std::ofstream output(path);
      output << report.dump(2) << '\n';
    }
  static bool itemLabQaDone = false;
  if (!itemLabQaDone && World && World->GetControlledCreature())
    if (const char *path = std::getenv("CUBA_ITEM_LAB_QA"))
    {
      if (const char *display = std::getenv("CUBA_ITEM_LAB_DISPLAY_QA"))
      {
        static bool configured = false;
        static std::chrono::steady_clock::time_point ready;
        if (!configured)
        {
          std::ifstream input(display);
          nlohmann::json request;
          input >> request;
          glfwSetWindowSize(Window, request.at("width"), request.at("height"));
          ApplyLiveUiScale(request.at("scale").get<float>());
          ready =
              std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
          configured = true;
          return;
        }
        if (std::chrono::steady_clock::now() < ready)
          return;
      }
      itemLabQaDone = true;
      RunItemLabQa(path);
    }
  static bool playerWindowQaDone = false;
  if (!playerWindowQaDone && PlayerWindowScreen && World && World->GetControlledCreature())
    if (const char *path = std::getenv("CUBA_PLAYER_WINDOW_QA"); path && *path)
    {
      playerWindowQaDone = true;
      RunPlayerWindowQa(path);
      if (const char *iconsPath = std::getenv("CUBA_INVENTORY_ICONS_QA"); iconsPath && *iconsPath)
        RunInventoryIconsQa(iconsPath);
      EnterInGameInputState();
    }
  static bool itemsQaDone = false;
  if (!itemsQaDone && World && World->GetControlledCreature())
    if (const char *path = std::getenv("CUBA_ITEMS_INVENTORY_QA"))
    {
      itemsQaDone = true;
      RunItemsInventoryQa(*World, path);
      RunOwnershipQa(*World, *GameSession,
                     std::string(path) + ".ownership.json");
      GameSession->SyncToWorldGameMode(World->GetGameMode());
      if (CharacterSheetScreen)
      {
        CharacterSheetScreen->RunInventoryQa(std::string(path) + ".ui.json");
        RunOwnershipUiQa(std::string(path) + ".ownership-ui.json");
        RunPlayerWindowQa(std::string(path) + ".player-window.json");
        RunInventoryIconsQa(std::string(path) + ".icons.json");
        CharacterSheetOpen = true;
        CharacterSheetScreen->SetSection(0);
        CharacterSheetScreen->SetVisible(true);
        SyncCursorVisibility();
      }
    }
  if (itemsQaDone && World && std::getenv("CUBA_ITEMS_CARRY_VISUAL_QA"))
  {
    static float visualTime = 0;
    visualTime += float(dt);
    CharacterSheetOpen = false;
    if (CharacterSheetScreen)
      CharacterSheetScreen->SetVisible(false);
    MinimalOverlayForBench = false;
    TickItemsCarryVisualQa(*World, visualTime);
  }
  else if (itemsQaDone && CharacterSheetScreen &&
           !std::getenv("CUBA_OWNERSHIP_CATALOG_VISUAL_QA") &&
           std::getenv("CUBA_ITEMS_INVENTORY_QA"))
  {
    static float qaTime = 0;
    qaTime += float(dt);
    CharacterSheetScreen->SetSection(std::min(3, int(qaTime / 6.f)));
  }
  if (itemsQaDone && World && std::getenv("CUBA_OWNERSHIP_CATALOG_VISUAL_QA"))
  {
    static float clock = 0;
    static int last = -1;
    clock += float(dt);
    const int phase = std::min(5, int(clock / 6.f));
    if (phase != last)
    {
      if (last < 0)
      {
        GameSession->SyncToWorldGameMode(WorldGameMode::Creative);
        auto &inv = World->GetControlledCreature()->GetInventory();
        for (const char *id :
             {"iron_pickaxe", "bucket", "apple", "iron_ingot", "wood_sword"})
        {
          InventoryEntryRef e;
          e.empty = false;
          e.kind = InventoryEntryKind::Item;
          e.Id = id;
          e.count = 1;
          inv.Insert(e);
        }
        InventoryEntryRef ref;
        ref.empty = false;
        ref.kind = InventoryEntryKind::Block;
        ref.Id = "stone";
        ref.count = 1;
        ref.form = InventoryEntryForm::CatalogReference;
        inv.AssignToHotbar(3, 9, ref);
      }
      if (phase < 2)
      {
        OpenPlayerWindow(4);
        if (PaletteScreen)
          PaletteScreen->OpenWithMainTab(phase == 0 ? 0 : 4);
      }
      else
        OpenPlayerWindow(phase - 2);
      last = phase;
    }
  }
  static bool appearanceUiQaDone = false;
  if (!appearanceUiQaDone && CharacterSheetScreen && World &&
      World->GetControlledCreature())
  {
    if (const char *path = std::getenv("CUBA_PLAYER_APPEARANCE_UI_QA"))
    {
      appearanceUiQaDone = true;
      CharacterSheetOpen = true;
      CharacterSheetScreen->RunAppearanceQa(path);
      SyncCursorVisibility();
    }
  }
}

void UApplication::RecordItemPerformanceFrame(double wallMs, double swapMs)
{
  if (ItemPerformanceQa && World && Geometry)
  {
    ItemPerformanceQa->Record(*World, Geometry->GetCreatureRenderStats(),
                              LastFramebufferWidth, LastFramebufferHeight,
                              wallMs, swapMs);
    if (ItemPerformanceQa->IsDone())
      RequestQuit();
  }
}

void UApplication::Update(double dt)
{
  // Audio listener: one tiny guarded copy per frame, no engine calls here.
  if (World)
  {
    if (const std::shared_ptr<UCamera> &camera = World->GetCurrentUserCamera())
    {
      const glm::vec3 position = camera->GetPosition();
      const glm::vec3 front = camera->GetFront();
      const glm::vec3 up = camera->GetUp();
      AudioListenerSnapshot listener;
      listener.X = position.x;
      listener.Y = position.y;
      listener.Z = position.z;
      listener.FwdX = front.x;
      listener.FwdY = front.y;
      listener.FwdZ = front.z;
      listener.UpX = up.x;
      listener.UpY = up.y;
      listener.UpZ = up.z;
      listener.Valid = true;
      UAudioApi::UpdateListener(listener);
    }

    // World context for ambience/music, throttled — biome sampling touches
    // the chunk cache, so 4 Hz is plenty for crossfade-paced scheduling.
    AudioAmbienceCooldown += dt;
    if (AudioAmbienceCooldown >= 0.25)
    {
      AudioAmbienceCooldown = 0.0;
      AudioAmbienceSnapshot ambience{};
      const glm::vec3 probe = World->GetCurrentUserCamera()
                                  ? World->GetCurrentUserCamera()->GetPosition()
                                  : World->GetSpawnPoint();
      std::snprintf(ambience.BiomeId, sizeof(ambience.BiomeId), "%s",
                    World->DominantBiomeIdAt(probe).c_str());
      std::snprintf(ambience.WeatherName, sizeof(ambience.WeatherName), "%s",
                    World->GetWeatherName().c_str());
      ambience.InGame = (State == AppState::InGame);
      ambience.IsNight = World->IsNightTime();
      ambience.Valid = true;
      UAudioApi::UpdateAmbience(ambience);
    }
  }
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
  RunPendingMenuAction();

  if (State == AppState::Loading)
  {
    if (PendingFirstWorldPaint && World)
    {
      FirstWorldPaintWaitMs += dt * 1000.0;
      if (!World->SampleRequiredPresentationCoverage(
              RequiredPresentationProbeMode::StopOnDebt).Ready())
      {
        ++FirstWorldPaintRepairFrames;
        if (!World->IsEnterLitGateActive())
          World->BeginEnterLitGate();
        World->ValidateRequiredEnterPresentation(FirstWorldPaintWaitMs);
        World->TickEnterWarmupDrainFrame(EnterWarmupMeshBudgetDefault(),
            std::max(1, URuntimeTuning::Get().EnterGateMeshDrainIterations), 12.0);
      }
      if (ProgressScreen)
        ProgressScreen->ApplySnapshot(ProgressSink.Get());
      GuiContext->Update(dt);
      SyncCursorVisibility();
      return;
    }
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
        // Era29: slightly higher streaming budget — SoftDefer/PendingLight on
        // bar.
        constexpr int kGpuWarmupMeshBudget = EnterWarmupMeshBudgetDefault();
        const int gate_iterations =
            std::max(1, URuntimeTuning::Get().EnterGateMeshDrainIterations);
        const int remaining =
            WorldOpRunner->EnterGameGpuWarmupFramesRemaining();
        const int frame = kGpuWarmupMaxFrames - remaining;
        EnterWarmupStepSample step_sample{};
        const auto &phys_before = World->GetPhysicsTelemetry();
        const int relight_completed_before = phys_before.RelightCompletedN;
        const int gpu_finish_before = phys_before.GpuFinishN;
        if (Geometry && World)
        {
          const bool coop_prepared =
              World->IsSpawnAreaPreparedByCooperativeLoad();
          const bool drain_cpu = ShouldDrainPreparedEnterWarmup(
              coop_prepared,
              World->HasPendingAsyncRelightWork() ||
                  World->GetPendingTerrainRelightFifoCount() > 0 ||
                  World->NeedsEnterGameMeshWarmup() ||
                  World->GetMeshService().GetAsyncInFlightCount() > 0 ||
                  World->GetMeshService().GetMeshCompletedSize() > 0 ||
                  World->GetMeshService().GetPendingCaptureCount() > 0 ||
                  World->GetMeshService().GetPendingGpuAppliesCount() > 0,
              World->IsEnterVisibilityReady(),
              World->IsEnterUnderfeetPresentReady(),
              World->SampleRequiredPresentationCoverage().Ready());
          if (frame == 0)
          {
            // Era51: coop PrepareView already warmed spawn — avoid GPU wipe.
            if (ShouldResetRenderStateForGpuWarmup(coop_prepared))
            {
              Geometry->ResetWorldRenderState();
              LogWorldLoadDiag("gpu_warmup_reset", *World);
            }
          }
          // Late async results can introduce debt after the first warmup frame.
          // Preserve prepared GPU storage, but acquire a consumer whenever the
          // actual queues or required section publications need one.
          if (!World->IsEnterLitGateActive() && drain_cpu)
          {
            UEnterLitDiagnostics::BeginSession("application_gpu_warmup");
            World->BeginEnterLitGate();
          }
          if (drain_cpu && World->IsEnterLitGateActive())
          {
            // Era46/47: shared enter drain frame — time-sliced per tick.
            const auto t0 = std::chrono::high_resolution_clock::now();
            World->TickEnterWarmupDrainFrame(kGpuWarmupMeshBudget,
                                             gate_iterations, 12.0);
            const double drain_frame_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::high_resolution_clock::now() - t0)
                    .count();
            step_sample.drain_mesh_ms = drain_frame_ms * 0.35;
            step_sample.gate_drain_ms = drain_frame_ms * 0.65;
            if (drain_frame_ms > 100.0)
            {
              CubatariumLogInfo("EnterWarmup",
                                "TickEnterWarmupDrainFrame ms=" +
                                    std::to_string(drain_frame_ms));
            }
          }
          else if (drain_cpu &&
                   ShouldRunEnterStreamingWarmupDespiteSpawnPrepared(
                       coop_prepared))
          {
            if (World->NeedsEnterGameMeshWarmup())
            {
              World->DrainEnterGameMeshWarmup(kGpuWarmupMeshBudget);
            }
            World->TickEnterStreamingWarmup(gate_iterations);
          }
          if (drain_cpu)
          {
            const auto t0 = std::chrono::high_resolution_clock::now();
            World->TickEnterFovLitPass(
                std::max(1, URuntimeTuning::Get().EnterFovLitCaptureBudget));
            step_sample.lit_pass_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::high_resolution_clock::now() - t0)
                    .count();
            if (step_sample.lit_pass_ms > 100.0)
            {
              CubatariumLogInfo("EnterWarmup",
                                "TickEnterFovLitPass ms=" +
                                    std::to_string(step_sample.lit_pass_ms));
            }
          }
          const auto &phys_after = World->GetPhysicsTelemetry();
          step_sample.relight_drain_ms = phys_after.RelightDrainMs;
          step_sample.mesh_emerge_ms = phys_after.MeshEmergeMs;
          step_sample.mesh_immediate_ms = phys_after.MeshImmediateMs;
          step_sample.mesh_emerge_prep_missing_ms =
              phys_after.MeshEmergePrepMissingMs;
          step_sample.mesh_emerge_prep_sticky_ms =
              phys_after.MeshEmergePrepStickyMs;
          step_sample.mesh_emerge_prep_drop_dirty_ms =
              phys_after.MeshEmergePrepDropDirtyMs;
          step_sample.relight_completed_delta =
              phys_after.RelightCompletedN - relight_completed_before;
          step_sample.gpu_finish_delta =
              phys_after.GpuFinishN - gpu_finish_before;
          UEnterLitDiagnostics::RecordFrameSteps(step_sample);
          const bool upload_ready =
              frame >= kGpuWarmupMinFrames - 1 &&
              (coop_prepared || !World->NeedsEnterGameMeshWarmup());
          if (upload_ready)
          {
            World->WarmupVisibleListAtCamera();
            if (ShouldWarmupGreedyGpuDuringEnter(
                    remaining, World->IsSpawnAreaPreparedByCooperativeLoad(),
                    frame, kGpuWarmupMinFrames))
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
    UpdateItemLab(dt);
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
      HudScreen->SetInteractionKeys(Ui.GrabKey, Ui.ThrowKey);
      HudScreen->Update(dt);
    }
    if (ConsoleScreen)
    {
      ConsoleScreen->Update(dt);
    }
    if (PlayerWindowScreen && (PaletteOpen || CharacterSheetOpen))
    {
      PlayerWindowScreen->SelectSection(
          PaletteOpen ? 4 : CharacterSheetScreen->GetSection());
      PlayerWindowScreen->Update(dt);
    }
    if (PaletteScreen)
    {
      PaletteScreen->Update(dt);
    }
    if (CraftingOpen && CraftingScreen)
    {
      CraftingScreen->Update(dt);
    }
    if (AnvilOpen && AnvilScreen)
    {
      AnvilScreen->Update(dt);
    }
    UpdateDeathOverlay();
    if (DeathScreenOpen && DeathScreen)
    {
      DeathScreen->Update(dt);
    }
    if (WorldGenScreen)
    {
      WorldGenScreen->Update(dt);
    }
    UpdateNativeItemsQa(dt);
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
  const float effective = ResolveEffectiveUiScale(Ui.UiScaleUser, metrics);
  SetUiScale(effective);
}

void UApplication::ApplyLiveUiScale(float user_scale)
{
  Ui.UiScaleUser = std::clamp(user_scale, kGuiMinUserScale, kGuiMaxUserScale);
  PlatformUiMetrics metrics = LastPlatformMetrics;
  metrics.ScreenWidthPx = LastFramebufferWidth;
  metrics.ScreenHeightPx = LastFramebufferHeight;
  const float effective = ResolveEffectiveUiScale(Ui.UiScaleUser, metrics);
  SetUiScale(effective);
}

void UApplication::ApplyLiveAudioSettings(const AudioSettings &settings)
{
  if (Core)
  {
    Core->ApplyAudioSettingsLive(settings);
  }
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
  if (PlayerWindowScreen)
  {
    PlayerWindowScreen->OnMetricsChanged(metrics);
  }
  else if (PaletteScreen)
  {
    PaletteScreen->OnMetricsChanged(metrics);
  }
  if (GuiContext && GuiContext->GetScreen())
  {
    GuiContext->GetScreen()->OnMetricsChanged(metrics);
  }
}

void UApplication::DrawPlayerFpViewmodel(int width, int height)
{
  if (ItemPerformanceQa)
    ItemPerformanceQa->BeginFpTiming();
  if (const UCreature *creature = World->GetControlledCreature())
  {
    const auto &inv = creature->GetInventory();
    FpViewmodelDrawParams fpParams;
    fpParams.FramebufferW = width;
    fpParams.FramebufferH = height;
    fpParams.CarryHands = creature->CarryHandCount;
    fpParams.ActorId = creature->GetId();
    fpParams.WorldGeneration = World->GetCreatureGeneration();
    ConfigureCarryContacts(fpParams, *World, *creature,
                           *World->GetCurrentUserCamera());
    fpParams.ThrowCharge = creature->ThrowCharge;
    fpParams.BowCharge = creature->BowCharge;
    fpParams.ItemUsePhase = creature->ActiveItemUsePhase();
    fpParams.ItemUseHeld = creature->ItemUse.Value > 0.f;
    fpParams.ItemLightIntensity = FpItemLight(*World, *creature);
    fpParams.ItemLightDirection =
        glm::mat3(World->GetCurrentUserCamera()->GetViewMatrix()) *
        glm::vec3(-.4f, .8f, .6f);
    fpParams.HasAmmo =
        inv.IsCreative() || inv.Count(InventoryEntryKind::Item, "arrow") > 0;
    fpParams.Active =
        creature->CarryHandCount ? nullptr : inv.GetActiveEntryRef();
    fpParams.Offhand = (creature->CarryHandCount || creature->BowCharge > 0)
                           ? nullptr
                           : &inv.GetEquippedOffhand();
    fpParams.SpeciesId = creature->GetTypeId();
    fpParams.Appearance = World->GetResolvedAppearanceShared(*creature);
    if (const auto definitions = World->GetCreatureDefinitionStorage())
      fpParams.Definition = definitions->Get(creature->GetTypeId());
    const auto *visual = creature->GetVisual();
    if (creature->CarryHandCount > 0 && visual && visual->HasWorldCarryArms() &&
        Geometry->GetRenderSettings().CreatureTexturedParts)
    {
      FpViewmodelRenderer->ClearHeldVisual();
      FpViewmodelRenderer->SetActiveItemHint({});
      if (ItemWorldVisualQa)
        ItemWorldVisualQa->RecordWorldCarry(*World);
    }
    else if (ItemWorldVisualQa)
    {
      FpViewmodelDrawAudit audit;
      fpParams.Audit = &audit;
      FpViewmodelRenderer->DrawWorldOverlay(fpParams);
      ItemWorldVisualQa->RecordFpAudit(fpParams, audit);
    }
    else
      FpViewmodelRenderer->DrawWorldOverlay(fpParams);
  }
  if (ItemPerformanceQa)
    ItemPerformanceQa->EndFpTiming();
}

void UApplication::RenderFrame(int width, int height, double viewDuration)
{
  const auto render_frame_begin = std::chrono::high_resolution_clock::now();
  if (World)
  {
    World->SetLastRenderFrameSetupMs(0.0);
    World->SetLastRenderEquipmentSetupMs(0.0);
    World->SetLastGeometryPaintMs(0.0);
    World->SetLastFpViewmodelMs(0.0);
  }

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
    CubatariumLogError("App",
                       "RenderFrame skipped viewport: zero framebuffer size");
    return;
  }
  if (PendingFirstWorldPaint && State == AppState::Loading && World && Geometry)
  {
    // Remain Loading through the platform's movement/streaming phase. The
    // prepared world's current inputs are checked in the same render call
    // which will paint it, rather than before a normal streaming tick.
    Geometry->PrepareFrameRendering();
    if (World->SampleRequiredPresentationCoverage(
            RequiredPresentationProbeMode::StopOnDebt).Ready())
    {
      World->EndEnterLitGate();
      PendingFirstWorldPaint = false;
      CubatariumLogInfo("FirstPaintBarrier", "ready_before_render wait_ms=" +
          std::to_string(FirstWorldPaintWaitMs) + " repair_frames=" +
          std::to_string(FirstWorldPaintRepairFrames));
      EnterGameAfterWorldChange();
    }
  }
  if (State == AppState::MainMenu || State == AppState::Loading)
  {
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    GuiContext->Render(width, height, ViewportInsetLeft, ViewportInsetTop,
                       ViewportInsetRight, ViewportInsetBottom);
    return;
  }

  if (ItemLabOpen && ItemLabScreen)
  {
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
    World->SetLastRenderFrameSetupMs(
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - render_frame_begin)
            .count());
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
      const auto equipment_setup_begin =
          std::chrono::high_resolution_clock::now();
      const WorldViewSettings &view = World->GetViewSettings();
      const auto camera = World->GetCurrentUserCamera();
      WornEquipmentDrawer::SetHidePossessedWield(
          ShouldDrawFpViewmodel(view) && camera &&
          camera->GetPerspective() == CameraPerspective::FirstPerson);
      if (Core)
      {
        if (auto items = Core->GetItemDefinitionStorage())
        {
          WornEquipmentDrawer::SetItemDefinitions(items.get());
        }
      }
      World->SetLastRenderEquipmentSetupMs(
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - equipment_setup_begin)
              .count());
      const auto geometry_paint_begin =
          std::chrono::high_resolution_clock::now();
      std::vector<ItemSurfaceDrawAudit> worldItems;
      {
        UItemSurfaceDrawAuditScope scope(ItemWorldVisualQa ? &worldItems
                                                           : nullptr);
        Geometry->Paint(width, height, viewDuration);
      }
      World->RecordFirstRenderedRequiredRing();
      if (ItemWorldVisualQa)
        ItemWorldVisualQa->RecordWorldItems(worldItems);
      World->SetLastGeometryPaintMs(
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - geometry_paint_begin)
              .count());
      WornEquipmentDrawer::SetHidePossessedWield(false);
    }
  }

  const auto fp_viewmodel_begin = std::chrono::high_resolution_clock::now();
  if (State == AppState::InGame && World &&
      (!MinimalOverlayForBench || std::getenv("CUBA_FLIGHT_QA_FP_SKIN") ||
       std::getenv("CUBA_FLIGHT_QA_FP_ITEM") ||
       std::getenv("CUBA_FLIGHT_QA_WORLD_USER")) &&
      !PaletteOpen && FpViewmodelRenderer)
  {
    const WorldViewSettings &view = World->GetViewSettings();
    if (const char *qaItem = std::getenv("CUBA_FLIGHT_QA_FP_ITEM");
        qaItem && *qaItem)
    {
      // Presentation-only probe: no inventory assignment, consumption or save.
      InventoryEntryRef main{InventoryEntryKind::Item, qaItem, 1, false};
      FpViewmodelDrawParams params;
      params.FramebufferW = width;
      params.FramebufferH = height;
      params.Active = &main;
      params.SpeciesId = "human";
      if (const auto definitions = World->GetCreatureDefinitionStorage())
        params.Definition = definitions->Get("human");
      if (const auto *creature = World->GetControlledCreature())
        params.Appearance = World->GetResolvedAppearanceShared(*creature);
      FpViewmodelRenderer->DrawWorldOverlay(params);
    }
    else if (const char *qaSkin = std::getenv("CUBA_FLIGHT_QA_FP_SKIN");
             qaSkin && std::getenv("CUBA_CREATURE_QA_MODELS"))
    {
      // Explicit candidate-only visual probe. Does not equip or save items.
      InventoryEntryRef main{InventoryEntryKind::Item, "wood_sword", 1, false};
      InventoryEntryRef offhand{InventoryEntryKind::Item, "wood_torch", 1,
                                false};
      FpViewmodelDrawParams fpParams;
      fpParams.FramebufferW = width;
      fpParams.FramebufferH = height;
      fpParams.Active = &main;
      fpParams.Offhand = &offhand;
      fpParams.SpeciesId = "human";
      if (const auto definitions = World->GetCreatureDefinitionStorage())
      {
        fpParams.Definition = definitions->Get("human");
        if (const auto skins = World->GetSkinDefinitionStorage())
          fpParams.Appearance =
              std::make_shared<const ResolvedCreatureAppearance>(
                  ResolveCreatureAppearance(*definitions, *skins, "human",
                                            qaSkin));
      }
      FpViewmodelRenderer->DrawWorldOverlay(fpParams);
    }
    else if (ShouldDrawFpViewmodel(view) && World->GetCurrentUserCamera() &&
             World->GetCurrentUserCamera()->GetPerspective() ==
                 CameraPerspective::FirstPerson)
    {
      DrawPlayerFpViewmodel(width, height);
    }
  }
  if (World)
  {
    World->SetLastFpViewmodelMs(
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - fp_viewmodel_begin)
            .count());
  }

  const auto gui_begin = std::chrono::high_resolution_clock::now();
  const bool hitch_gui = World && (World->GetWallFrameDelta() * 1000.0) > 24.0;
  if (State == AppState::InGame && IconSource && !MinimalOverlayForBench &&
      !hitch_gui)
  {
    IconSource->WarmupObjectIcons(2);
    IconSource->WarmupCreatureIcons(2);
    // UX U3: item icons join the budgeted prewarm so the first inventory
    // open finds them ready.
    IconSource->WarmupItemIcons(2);
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
    GuiContext->RenderOverlay(*ConsoleScreen->GetRoot(), width, height, false);
  }
  if (OverlayPopup && OverlayPopup->IsOpen())
  {
    auto &renderer = GuiContext->GetRenderer();
    renderer.BeginFrame(width, height);
    OverlayPopup->Draw(renderer);
    renderer.EndFrame();
  }
  if ((PaletteOpen || CharacterSheetOpen) && PlayerWindowScreen)
  {
    auto *root = GetPlayerWindowRoot();
    notifyViewport(PlayerWindowScreen.get());
    if (PaletteOpen)
      PaletteScreen->RenderPreview();
    GuiContext->RenderOverlay(*root, width, height, false);
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
  if (WorldGenOpen && WorldGenScreen && WorldGenScreen->GetRoot())
  {
    notifyViewport(WorldGenScreen.get());
    WorldGenScreen->RenderPreview();
    GuiContext->RenderOverlay(*WorldGenScreen->GetRoot(), width, height, false);
  }
#if defined(__ANDROID__)
  if (HudScreen && (PaletteOpen || WorldGenOpen || CraftingOpen || AnvilOpen))
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
  if (ItemWorldVisualQa && World &&
      ItemWorldVisualQa->Capture(*World, *this, width, height))
    RequestQuit();
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
  return ItemLabOpen || ConsoleOpen || PaletteOpen || WorldGenOpen ||
         CharacterSheetOpen || CraftingOpen ||
         AnvilOpen || DeathScreenOpen || GuiContext->WantsCaptureKeyboard();
}

bool UApplication::AllowsWorldMousePlacement() const
{
  return State == AppState::InGame && !ItemLabOpen && !ConsoleOpen &&
         !PaletteOpen && !WorldGenOpen && !CharacterSheetOpen &&
         !CraftingOpen && !AnvilOpen &&
         !DeathScreenOpen && !DeathSpectating &&
         !(World && World->IsHardcoreDead());
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

void ApplicationBlockCatalogCallback::operator()()const {owner->RefreshBlockCatalog();}
void ApplicationCreatureCatalogCallback::operator()()const {
  owner->RefreshCreatureCatalog();
  if(owner->IconSource)owner->IconSource->ClearCreatureIconCache();
  if(owner->CreaturePreviewRenderer)owner->CreaturePreviewRenderer->Invalidate();
  if(owner->PaletteScreen)owner->PaletteScreen->InvalidateGrid();
}



} // namespace cutum
