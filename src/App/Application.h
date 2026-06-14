#ifndef APPLICATION_H
#define APPLICATION_H

#include "App/Settings/AppSettingsSnapshot.h"
#include "App/Settings/AppState.h"
#include "App/Platform/CursorCapture.h"
#include "Game/GameSession.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Interfaces/IGuiClipboard.h"
#include "Gui/Interfaces/IGuiMenuHost.h"
#include "Gui/Screens/ConsoleScreen.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Screens/InGameHudScreen.h"
#include "Gui/Widgets/GuiPopupMenu.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "Game/Inventory/SlotInteraction.h"
#include "App/Settings/UiSettings.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace cutum
{

class TouchInputBridge;

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

enum class MenuSubview
{
  Main,
  Settings,
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
  void SetTouchInputBridge(TouchInputBridge *bridge) { touchBridge_ = bridge; }
  TouchInputBridge *GetTouchInputBridge() const { return touchBridge_; }
  bool IsQuitRequested() const { return QuitRequested; }
  void HandleWindowFocus(bool focused);

  void Update(double dt);
  void ProcessInput();
  void RenderFrame(int width, int height, double viewDuration);

  bool RouteKey(int key, int action, int mods);
  bool RouteChar(unsigned int codepoint);
  bool RouteMouseButton(int button, bool pressed, int x, int y);
  bool RouteMouseMove(int x, int y);
  bool RouteScroll(double xoffset, double yoffset, int mouseX, int mouseY);

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
  int GetHotbarCountSetting() const { return Ui.hotbarCount; }
  void SetHotbarCountSetting(int count);

  void ReturnToMainMenu() override;
  void SaveIfNeededAndProceed(std::function<void()> proceed) override;
  AppSettingsSnapshot LoadAppSettingsSnapshot() const override;
  ProceduralSettings LoadProceduralTemplate() const override;
  void
  SaveAppAndTemplateSettings(const AppSettingsSnapshot &app,
                             const ProceduralSettings &procedural) override;
  void CreateNewWorldWithSettings(const ProceduralSettings &settings) override;
  void LoadSelectedWorld(const std::string &worldName) override;
  void RefreshWorldList() override;
  const std::vector<std::string> &GetWorldNames() const override;

  void ShowSettings();
  void ShowNewWorld();
  void ShowLoadWorld();

private:
  void ShowMainMenu();
  void SaveActiveWorldIfNeeded();
  void ScheduleDeferredMenuAction(std::function<void()> action);
  void EnterGameAfterWorldChange();
  void ShowInGameHud();
  void SyncCursorVisibility();
  AppCursorPolicy GetCursorPolicy() const;
  void EnterInGameInputState();
  /// Выход из UI-only (Left Alt): временно свободный курсор для HUD.
  void RecaptureMouseForLook();
  bool UsesUiPointer() const;
  bool BlocksGameMouseLook() const;
  bool TryRouteInGameOverlay(const GuiMouseEvent &event, bool pressed);
  bool ResolveSlotAt(int x, int y, SlotAddress &out);
  void DrawDragGhost(int width, int height);
  void ClearGameplayKeyboard();

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
  OverlayPointerCapture ActiveOverlayCapture{OverlayPointerCapture::None};
  int DragCursorX{0};
  int DragCursorY{0};
  bool WorldSessionActive{false};
  bool PendingEnterGame{false};
  bool PendingQuit{false};
  std::function<void()> PendingMenuAction;
  bool QuitRequested{false};

  std::unique_ptr<UGuiIconSource> IconSource;
  std::unique_ptr<UInGameHudScreen> HudScreen;
  std::unique_ptr<UConsoleScreen> ConsoleScreen;
  std::unique_ptr<UCreativePaletteScreen> PaletteScreen;
  std::unique_ptr<IGuiClipboard> Clipboard;
  std::unique_ptr<UGuiPopupMenu> OverlayPopup;

  MenuSubview MenuSubview{MenuSubview::Main};
  UMainMenuScreen *MainMenuScreen{nullptr};
  TouchInputBridge *touchBridge_{nullptr};
};

} // namespace cutum

#endif
