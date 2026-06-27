#ifndef APPLICATION_H
#define APPLICATION_H

#include "App/Platform/CursorCapture.h"
#include "App/Settings/AppSettingsSnapshot.h"
#include "App/Settings/AppState.h"
#include "App/Settings/UiSettings.h"
#include "App/WorldOperationRunner.h"
#include "Core/Progress/IProgressSink.h"
#include "Game/GameSession.h"
#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiMetrics.h"
#include "Gui/Interfaces/IGuiClipboard.h"
#include "Gui/Interfaces/IGuiMenuHost.h"
#include "Gui/Screens/ConsoleScreen.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Screens/InGameHudScreen.h"
#include "Gui/Screens/WorldProgressScreen.h"
#include "Gui/Widgets/GuiPopupMenu.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <array>
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
class UGameSession;
class UBlockDefinitionStorage;
class UGuiIconSource;
class UMainMenuScreen;
class UWorldResourcePacksScreen;

enum class MenuSubview
{
  Main,
  Settings,
  WorldSettings,
  LoadWorld,
  NewWorld
};

class UApplication : public IGuiMenuHost
{
public:
  UApplication(std::shared_ptr<UCore> core, std::shared_ptr<UWorld> world,
               std::shared_ptr<UGeometryEngine> geometry,
               std::shared_ptr<UViewEngine> views,
               std::shared_ptr<UTextRenderer> text_renderer,
               std::shared_ptr<UShaderManager> shader_manager,
               std::shared_ptr<UBlockDefinitionStorage> block_definitions);
  ~UApplication();

  void Startup(const std::string &configPath);
  void RequestEnterGame();
  /// Вход в игру на следующем кадре (безопасно из onClick кнопки меню).
  void ScheduleEnterGame();
  void ScheduleQuit();
  void RequestQuit();
  void SetWindow(GLFWwindow *window) { Window = window; }
  void SetTouchInputBridge(UTouchInputBridge *bridge) { TouchBridge = bridge; }
  UTouchInputBridge *GetTouchInputBridge() const { return TouchBridge; }
  bool IsQuitRequested() const { return QuitRequested; }
  void HandleWindowFocus(bool focused);

  void Update(double dt);
  void ProcessInput();
  void RenderFrame(int width, int height, double viewDuration);
  void SetViewportInsets(int left, int top, int right, int bottom);
  void SetKeyboardInsetBottom(int bottom);
  void SetUiScale(float scale);
  float GetUiScale() const { return UiScale; }
  void UpdateUiScale(int fb_w, int fb_h, const PlatformUiMetrics &platform);
  void ApplyLiveUiScale(float user_scale) override;

  bool RouteKey(int key, int Action, int Mods);
  bool RouteChar(unsigned int Codepoint);
  bool RouteMouseButton(int Button, bool Pressed, int x, int y,
                        int PointerId = -1);
  bool RouteMouseMove(int x, int y, int PointerId = -1);
  bool RouteScroll(double Xoffset, double Yoffset, int mouseX, int mouseY);
#if defined(__ANDROID__)
  void ReleaseHudJoystickCapture();
  void TryToggleFlightOnJumpPress();
  void SubmitConsoleCommand();
#endif

  AppState GetState() const { return State; }
  bool HasWorldSession() const { return WorldSessionActive; }
  UGuiContext &GetGui() { return *GuiContext; }
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

  void ShowSettings();
  void ShowWorldSettings();
  void ShowNewWorld();
  void ShowLoadWorld();
  void BeginWorldOperation(WorldRunnerRequest request,
                           std::function<void()> onComplete = nullptr);
  void OnWorldOperationFinished();

private:
  void ShowWorldProgressScreen();
  void ShowMainMenu();
  void SaveActiveWorldIfNeeded();
  void ScheduleDeferredMenuAction(std::function<void()> Action);
  void EnterGameAfterWorldChange();
  void RefreshBlockCatalog();
  void ShowInGameHud();
  void SyncCursorVisibility();
  void SyncGameplayLookCapture();
  AppCursorPolicy GetCursorPolicy() const;
  void EnterInGameInputState();
  /// Выход из UI-only (Left Alt): временно свободный курсор для HUD.
  void RecaptureMouseForLook();
  bool UsesUiPointer() const;
  bool BlocksGameMouseLook() const;
  bool TryRouteInGameOverlay(const GuiMouseEvent &event, bool Pressed);
  bool HasAnyOverlayCapture() const;
  bool ResolveSlotAt(int x, int y, SlotAddress &out);
  void DrawDragGhost(int width, int height);
  void ClearGameplayKeyboard();
  void NotifyAllScreensMetricsChanged(const GuiMetrics &metrics);

  std::shared_ptr<UCore> Core;
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
  bool FreeCursor{false};
  /// Подавить следующий glfw char после открытия консоли (символ
  /// клавиши-тоггла).
  bool SuppressConsoleToggleChar{false};
  enum class OverlayPointerCapture
  {
    None,
    Palette,
    Console,
    Hud
  };
  static constexpr int kMaxOverlayPointers = 10;
  std::array<OverlayPointerCapture, kMaxOverlayPointers> OverlayCaptures{};
  int NormalizeOverlayPointer(int PointerId) const;
  int DragCursorX{0};
  int DragCursorY{0};
  bool WorldSessionActive{false};
  bool PendingEnterGame{false};
  bool PendingQuit{false};
  std::function<void()> PendingMenuAction;
  std::function<void()> WorldOpOnComplete;
  bool QuitRequested{false};

  UWorldProgressScreen *ProgressScreen{nullptr};
  std::unique_ptr<UWorldOperationRunner> WorldOpRunner;
  ULatestProgressSink ProgressSink;

  std::unique_ptr<UGuiIconSource> IconSource;
  std::unique_ptr<UInGameHudScreen> HudScreen;
  std::unique_ptr<UConsoleScreen> ConsoleScreen;
  std::unique_ptr<UCreativePaletteScreen> PaletteScreen;
  std::unique_ptr<IGuiClipboard> Clipboard;
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
