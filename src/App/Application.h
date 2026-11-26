#include "Core/Memory/MemberCallbackStorage.h"
#ifndef APPLICATION_H
#define APPLICATION_H

#include "App/Input/InputRouter.h"
#include "App/Navigation/ScreenNavigator.h"
#include "App/Platform/CursorCapture.h"
#include "App/Settings/AppSettingsSnapshot.h"
#include "App/Settings/AppState.h"
#include "App/Settings/UiSettings.h"
#include "App/WorldOperationRunner.h"
#include "App/ApplicationOperationStorage.h"
#include "Core/Progress/IUProgressSink.h"
#include "Game/GameSession.h"
#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiMetrics.h"
#include "Gui/Interfaces/IUGuiClipboard.h"
#include "Gui/Interfaces/IUGuiMenuHost.h"
#include "Gui/Screens/AnvilScreen.h"
#include "Gui/Screens/CharacterSheetScreen.h"
#include "Gui/Screens/ConsoleScreen.h"
#include "Gui/Screens/CraftingScreen.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Screens/DeathScreen.h"
#include "Gui/Screens/InGameHudScreen.h"
#include "Gui/Screens/PlayerWindowScreen.h"
#include "Gui/Screens/WorldGenPaletteScreen.h"
#include "Gui/Screens/WorldProgressScreen.h"
#include "Gui/Widgets/GuiPopupMenu.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace cutum
{

class UTouchInputBridge;

class UCore;
class UWorld;
class UGeometryEngine;
class UViewEngine;
class UTextRenderer;
class UShaderManager;
class UGuiContext;
class UGuiWidget;
class UGameSession;
class UBlockDefinitionStorage;
class UGuiIconSource;
struct ObjectIconStorage;
struct InventoryIconStorage;
class UContentPreviewRenderer;
struct ContentPreviewRendererStorage;
class UCreaturePreviewRenderer;
class UFpViewmodelRenderer;
enum class FpSwingKind : uint8_t;
class UMainMenuScreen;
struct MainMenuStorage;
class UItemLabScreen;
class UWorldResourcePacksScreen;

enum class MenuSubview
{
  Main,
  Settings,
  WorldSettings,
  LoadWorld,
  NewWorld
};

class UApplication : public IUGuiMenuHost
{
  friend struct ApplicationBlockCatalogCallback;
  friend struct ApplicationCreatureCatalogCallback;
  friend struct ApplicationShowMenuAction;
  friend struct ApplicationScheduleRetrySaveAction;
  friend struct ApplicationScheduleShowMenuAction;
  friend struct ApplicationShutdownCompleteAction;
  friend struct ApplicationOperationSourceProbe;
  friend class UDeathScreen;
  friend class UScreenNavigator;
  friend class UInputRouter;
public:
  MemberCallbackStorageSnapshot InspectStoredActionCallbacks(bool known) const noexcept;
  ApplicationOperationStorage InspectOperationStorage(const AllocatorSourcePhase &phase,bool known) const noexcept;

  template<class Inspector> void InspectCatalogDomain(Inspector &out) const {
    out.BlockStorage(BlockDefinitions,"application");out.Core(Core.get());out.World(World.get());out.Geometry(Geometry.get());
    out.GuiIcon(IconSource.get());out.Preview(ContentPreviewRenderer.get());out.Viewmodel(FpViewmodelRenderer.get());out.ItemLab(ItemLabScreen);
  }
  friend void RunSaveErrorUiProbe(UApplication &application, UCore &core);

public:
  UApplication(std::shared_ptr<UCore> core, std::shared_ptr<UWorld> world,
               std::shared_ptr<UGeometryEngine> geometry,
               std::shared_ptr<UViewEngine> views,
               std::shared_ptr<UTextRenderer> text_renderer,
               std::shared_ptr<UShaderManager> shader_manager,
               std::shared_ptr<UBlockDefinitionStorage> block_definitions);
  ~UApplication();
  const std::shared_ptr<UWorld> &GetWorldRootForStorage() const noexcept {return World;}
  const std::shared_ptr<UGeometryEngine> &GetGeometryRootForStorage() const noexcept {return Geometry;}

  void Startup(const std::string &configPath);
  bool StartupSucceeded() const { return StartupOk; }
  void RequestEnterGame();
  /// Вход в игру на следующем кадре (безопасно из onClick кнопки меню).
  void ScheduleEnterGame();
  void ScheduleQuit();
  void RequestQuit();
  /// Window close / quit: cooperative shutdown with progress UI.
  bool TryBeginShutdownFromWindowClose();
  void BeginShutdownOperation(bool saveSession, bool closeAfter);
  void PrepareForShutdown();
  void SetWindow(GLFWwindow *window) { Window = window; }
  /// Flight-sim / benches: skip HUD + icon warmup so wall gates measure
  /// stream/mesh.
  void SetMinimalOverlayForBench(bool enabled)
  {
    MinimalOverlayForBench = enabled;
  }
  void SetTouchInputBridge(UTouchInputBridge *bridge) { TouchBridge = bridge; }
  UTouchInputBridge *GetTouchInputBridge() const { return TouchBridge; }
  bool IsQuitRequested() const { return QuitRequested; }
  void ShowFatalSaveFailure(const std::string &message);
  void HandleWindowFocus(bool focused);

  void Update(double dt);
  void UpdateNativeItemsQa(double dt);
  void RecordItemPerformanceFrame(double wallMs, double swapMs);
  void UpdateItemLab(double dt);
  void OpenPlayerWindow(int section);
  UGuiWidget *GetPlayerWindowRoot();
  void RunPlayerWindowQa(const std::string &path);
  void RunInventoryIconsQa(const std::string &path);
  void ToggleItemLab();
  void RunItemLabQa(const std::string &path);
  void ShowInteractionMessage(const std::string &message, float seconds = 2.f);

  void ProcessInput();
  void DrawPlayerFpViewmodel(int width, int height);
  void RenderFrame(int width, int height, double viewDuration);
  void NotifyFpSwing(FpSwingKind kind);
  void NotifyFpUseVisual(const std::string &presetId, bool hold);
  void ClearFpHeldVisual();
  void EndFpUseVisual();
  void OpenCraftingScreen();
  void OpenAnvilScreen();
  void SetViewportInsets(int left, int top, int right, int bottom);
  void SetKeyboardInsetBottom(int bottom);
  void SetUiScale(float scale);
  float GetUiScale() const { return UiScale; }
  void UpdateUiScale(int fb_w, int fb_h, const PlatformUiMetrics &platform);
  void ApplyLiveUiScale(float user_scale) override;
  void ApplyLiveAudioSettings(const AudioSettings &settings) override;

  bool RouteKey(int key, int Action, int Mods);
  bool RouteChar(unsigned int Codepoint);
  bool RouteMouseButton(int Button, bool Pressed, int x, int y,
                        int PointerId = -1);
  bool RouteMouseMove(int x, int y, int PointerId = -1);
  bool RouteScroll(double Xoffset, double Yoffset, int mouseX, int mouseY);
#if defined(__ANDROID__)
  void ReleaseHudJoystickCapture();
  void ReleaseHudJoystickCaptureForPointer(int pointerId);
  void TryToggleFlightOnJumpPress();
  void SubmitConsoleCommand();
#endif

  AppState GetState() const { return State; }
  bool HasWorldSession() const { return WorldSessionActive; }
  UGuiContext &GetGui() { return *GuiContext; }
  const UGuiContext &GetGui() const { return *GuiContext; }
  MainMenuStorage InspectMainMenuStorage() const noexcept;
  InGameHudStorage InspectHudStorage() const noexcept;
  ConsoleStorage InspectConsoleStorage() const noexcept;
  DeathScreenStorage InspectDeathScreenStorage() const noexcept;
  PlayerWindowStorage InspectPlayerWindowStorage() const noexcept;
  PaletteGridStorage InspectPaletteGridStorage() const noexcept;
  WorldGenPaletteStorage InspectWorldGenPaletteStorage() const noexcept;
  ContentPreviewRendererStorage InspectContentPreviewStorage() const noexcept;
  ObjectIconStorage InspectObjectIconStorage() const noexcept;
  InventoryIconStorage InspectInventoryIconStorage() const noexcept;
  UGameSession &GetGameSession() { return *GameSession; }
  bool WantsCaptureMouse() const;
  bool WantsCaptureKeyboard() const;
  /// ЛКМ в мир (постановка блока/префаба) при закрытых палитре и консоли, в
  /// т.ч. с видимым курсором (Left Alt).
  bool AllowsWorldMousePlacement() const;
  const UiSettings &GetUiSettings() const { return Ui; }
  int GetHotbarCountSetting() const { return Ui.HotbarCount; }
  void SetHotbarCountSetting(int count);

  void ReturnToMainMenu() override;
  void SaveWorldSessionIfNeeded();
  void SaveIfNeededAndProceed(std::function<void()> proceed) override;
  AppSettingsSnapshot LoadAppSettingsSnapshot() const override;
  ProceduralSettings LoadProceduralTemplate() const override;
  void
  SaveAppAndTemplateSettings(const AppSettingsSnapshot &app,
                             const ProceduralSettings &procedural) override;
  void CreateNewWorldWithSettings(
      const ProceduralSettings &settings,
      const std::vector<std::string> &resourcePacksEnabled) override;
  void
  CreateNewWorldWithSettings(const ProceduralSettings &settings,
                             const ResourcePackSelection &selection) override;
  void CreateNewWorldWithSettings(const ProceduralSettings &settings,
                                  const ResourcePackSelection &selection,
                                  const WorldViewSettings &view) override;
  void CreateNewWorldWithSettings(const ProceduralSettings &settings,
                                  const ResourcePackSelection &selection,
                                  const WorldViewSettings &view,
                                  WorldGameMode gameMode) override;
  void CreateNewWorldWithSettings(const ProceduralSettings &settings,
                                  const ResourcePackSelection &selection,
                                  const WorldViewSettings &view,
                                  WorldGameMode gameMode,
                                  WorldDifficulty difficulty) override;
  void LoadSelectedWorld(const std::string &worldName) override;
  void RefreshWorldList() override;
  const std::vector<std::string> &GetWorldNames() const override;
  std::vector<InstalledPackInfo> ListInstalledResourcePacks() const override;
  std::vector<std::string> GetDefaultEnabledResourcePacks() const override;
  ResourcePackSelection GetDefaultResourcePackSelection() const override;
  std::vector<std::string>
  PeekWorldResourcePacks(const std::string &worldName) const override;
  ResourcePackSelection GetCurrentWorldResourcePackSelection() const override;
  bool ApplyResourcePacksToCurrentWorld(
      const ResourcePackSelection &selection) override;
  ThrowSettings GetCurrentThrowSettings() const override;
  bool ApplyThrowSettings(const ThrowSettings &) override;
  WorldViewSettings GetCurrentWorldViewSettings() const override;
  bool ApplyViewSettingsToCurrentWorld(const WorldViewSettings &view) override;
  bool ApplyWorldSettings(const ResourcePackSelection &selection,
                          const WorldViewSettings &view) override;

  void ShowSettings();
  void ShowWorldSettings();
  void ShowNewWorld();
  void ShowLoadWorld();
  void CloseCreativePalette();
  void BeginWorldOperation(WorldRunnerRequest request,
                           std::function<void()> onComplete = nullptr);
  void OnWorldOperationFinished();

private:
  void ShowWorldProgressScreen();
  void ShowMainMenu();
  void SaveActiveWorldIfNeeded();
  void ScheduleDeferredMenuAction(std::function<void()> Action);
  void RunPendingMenuAction();
  void EnterGameAfterWorldChange();
  void RefreshBlockCatalog();
  void RefreshCreatureCatalog();
  void ShowInGameHud();
  void BuildDeathOverlay();
  void BuildWorldGenOverlay();
  void CloseCraftingScreen();
  void CloseAnvilScreen();
  void CloseWorldGenScreen();
  void UpdateDeathOverlay();
  void SetDeathOverlayVisible(bool visible);
  void RespawnPlayerFromDeathScreen();
  void SpectateFromDeathScreen();
  void ScheduleReturnFromDeathScreen();
  void SyncCursorVisibility();
  void SyncGameplayLookCapture();
  /// After projection change: visible cursor for iso, refresh aim ray.
  void OnGameplayViewProjectionChanged();
  AppCursorPolicy GetCursorPolicy() const;
  void EnterInGameInputState();
  /// Выход из UI-only (Left Alt): временно свободный курсор для HUD.
  void RecaptureMouseForLook();
  bool UsesUiPointer() const;
  bool BlocksGameMouseLook() const;
  void RunOwnershipUiQa(const std::string &path);
  bool TryRouteInGameOverlay(const GuiMouseEvent &event, bool Pressed);
  bool HasAnyOverlayCapture() const;
  /// End inventory/overlay pointer gesture: drop or cancel drag, clear Pressed.
  void FinishInventoryPointerGesture(const GuiMouseEvent &event);
  bool ResolveSlotAt(int x, int y, SlotAddress &out);
  void DrawDragGhost(int width, int height);
  void ClearGameplayKeyboard();
  void NotifyAllScreensMetricsChanged(const GuiMetrics &metrics);

  UScreenNavigator ScreenNav;
  UInputRouter InputRouter;

  std::shared_ptr<UCore> Core;
  /// Throttle for the throttled ambience snapshot post (see Update).
  double AudioAmbienceCooldown{0.0};
  std::shared_ptr<UWorld> World;
  std::shared_ptr<UGeometryEngine> Geometry;
  std::shared_ptr<UViewEngine> Views;
  std::shared_ptr<UTextRenderer> TextRenderer;
  std::shared_ptr<UShaderManager> ShaderManager;
  std::shared_ptr<UBlockDefinitionStorage> BlockDefinitions;

  std::unique_ptr<UGuiContext> GuiContext;
  std::unique_ptr<UGameSession> GameSession;

  AppState State{AppState::MainMenu};
  UiSettings Ui;
  GLFWwindow *Window{nullptr};
  bool ConsoleOpen{false};
  bool PaletteOpen{false};
  bool WorldGenOpen{false};
  bool CharacterSheetOpen{false};
  bool CraftingOpen{false};
  bool AnvilOpen{false};
  bool DeathScreenOpen{false};
  bool DeathSpectating{false};
  bool FreeCursor{false};
  /// Подавить следующий glfw char после открытия консоли (символ
  /// клавиши-тоггла).
  bool SuppressConsoleToggleChar{false};
  enum class OverlayPointerCapture
  {
    None,
    Palette,
    Console,
    Hud,
    WorldGen,
    CharacterSheet,
    Crafting,
    Anvil,
    Death
  };
  void CancelOverlayCapture(OverlayPointerCapture capture);
  static constexpr int kMaxOverlayPointers = 10;
  std::array<OverlayPointerCapture, kMaxOverlayPointers> OverlayCaptures{};
  /// Leaf widget that received overlay mouse-down (slot owner for teardown).
  UGuiWidget *OverlayPressedWidget{nullptr};
  int NormalizeOverlayPointer(int PointerId) const;
  int DragCursorX{0};
  int DragCursorY{0};
  bool WorldSessionActive{false};
  bool PendingEnterGame{false};
  bool PendingQuit{false};
  bool PendingShutdownSave{false};
  bool ShutdownCloseAfter{false};
  bool StartupOk{false};
  bool MinimalOverlayForBench{false};
  std::function<void()> PendingMenuAction;
  std::function<void()> WorldOpOnComplete;
  bool QuitRequested{false};

  UWorldProgressScreen *ProgressScreen{nullptr};
  bool PendingFirstWorldPaint{false};
  double FirstWorldPaintWaitMs{0.0};
  unsigned FirstWorldPaintRepairFrames{0};
  std::unique_ptr<UWorldOperationRunner> WorldOpRunner;
  ULatestProgressSink ProgressSink;

  std::unique_ptr<UGuiIconSource> IconSource;
  std::shared_ptr<UCreaturePreviewRenderer> CreaturePreviewRenderer;
  std::unique_ptr<UContentPreviewRenderer> ContentPreviewRenderer;
  std::unique_ptr<UFpViewmodelRenderer> FpViewmodelRenderer;
  std::unique_ptr<UInGameHudScreen> HudScreen;
  std::unique_ptr<UConsoleScreen> ConsoleScreen;
  std::unique_ptr<UCreativePaletteScreen> PaletteScreen;
  std::unique_ptr<UCraftingScreen> CraftingScreen;
  std::unique_ptr<UAnvilScreen> AnvilScreen;
  std::unique_ptr<UDeathScreen> DeathScreen;
  std::unique_ptr<UCharacterSheetScreen> CharacterSheetScreen;
  std::unique_ptr<UPlayerWindowScreen> PlayerWindowScreen;
  std::unique_ptr<UWorldGenPaletteScreen> WorldGenScreen;
  UItemLabScreen *ItemLabScreen{nullptr}; // Owned by GuiContext while active.
  bool ItemLabOpen{false};
  std::unique_ptr<IUGuiClipboard> Clipboard;
  std::unique_ptr<UGuiPopupMenu> OverlayPopup;

  MenuSubview MenuSubview{MenuSubview::Main};
  UMainMenuScreen *MainMenuScreen{nullptr};
  UTouchInputBridge *TouchBridge{nullptr};
  float UiScale{1.f};
  PlatformUiMetrics LastPlatformMetrics{};
  int LastFramebufferWidth{0};
  int LastFramebufferHeight{0};
  int ViewportInsetLeft{0};
  int ViewportInsetTop{0};
  int ViewportInsetRight{0};
  int ViewportInsetBottom{0};
  int KeyboardInsetBottom{0};
  AppCursorPolicy LastCursorPolicy{AppCursorPolicy::Free};
};

} // namespace cutum

#endif
