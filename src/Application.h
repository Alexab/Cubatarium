#ifndef APPLICATION_H
#define APPLICATION_H

#include "AppState.h"
#include "CursorCapture.h"
#include "UiSettings.h"
#include "Gui/Interfaces/IGuiClipboard.h"
#include "Gui/Screens/ConsoleScreen.h"
#include "Gui/Widgets/GuiPopupMenu.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/GuiContext.h"
#include "Gui/Screens/InGameHudScreen.h"
#include "AppSettingsSnapshot.h"
#include "Game/GameSession.h"
#include "SlotInteraction.h"
#include "Gui/Interfaces/IGuiMenuHost.h"
#include "ProceduralSettings.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace cutum {

class Core;
class World;
class GeometryEngine;
class ViewEngine;
class TextRenderer;
class ShaderManager;
class GuiContext;
class GameSession;
class BlockDefinitionStorage;
class GuiIconSource;
class MainMenuScreen;

enum class MenuSubview { Main, Settings, LoadWorld, NewWorld };

class Application : public IGuiMenuHost {
public:
    Application(std::shared_ptr<Core> core,
                std::shared_ptr<World> world,
                std::shared_ptr<GeometryEngine> geometry,
                std::shared_ptr<ViewEngine> views,
                std::shared_ptr<TextRenderer> textRenderer,
                std::shared_ptr<ShaderManager> shaderManager,
                std::shared_ptr<BlockDefinitionStorage> blockDefinitions);
    ~Application();

    void Startup(const std::string& configPath);
    void RequestEnterGame();
    /// Вход в игру на следующем кадре (безопасно из onClick кнопки меню).
    void ScheduleEnterGame();
    void ScheduleQuit();
    void RequestQuit();
    void SetWindow(GLFWwindow* window) { window_ = window; }
    void HandleWindowFocus(bool focused);

    void Update(double dt);
    void ProcessInput();
    void RenderFrame(int width, int height, double viewDuration);

    bool RouteKey(int key, int action, int mods);
    bool RouteChar(unsigned int codepoint);
    bool RouteMouseButton(int button, bool pressed, int x, int y);
    bool RouteMouseMove(int x, int y);
    bool RouteScroll(double xoffset, double yoffset, int mouseX, int mouseY);

    AppState GetState() const { return state_; }
    bool HasWorldSession() const { return worldSessionActive_; }
    GuiContext& GetGui() { return *guiContext_; }
    GameSession& GetGameSession() { return *gameSession_; }
    bool WantsCaptureMouse() const;
    bool WantsCaptureKeyboard() const;
    /// ЛКМ в мир (постановка блока/префаба) при закрытых палитре и консоли, в т.ч. с видимым курсором (Right Alt).
    bool AllowsWorldMousePlacement() const;
    const UiSettings& GetUiSettings() const { return uiSettings_; }
    int GetHotbarCountSetting() const { return uiSettings_.hotbarCount; }
    void SetHotbarCountSetting(int count);

    void ReturnToMainMenu() override;
    void SaveIfNeededAndProceed(std::function<void()> proceed) override;
    AppSettingsSnapshot LoadAppSettingsSnapshot() const override;
    ProceduralSettings LoadProceduralTemplate() const override;
    void SaveAppAndTemplateSettings(const AppSettingsSnapshot& app,
                                    const ProceduralSettings& procedural) override;
    void CreateNewWorldWithSettings(const ProceduralSettings& settings) override;
    void LoadSelectedWorld(const std::string& worldName) override;
    void RefreshWorldList() override;
    const std::vector<std::string>& GetWorldNames() const override;

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
    /// Выход из UI-only (Right Alt): обзор снова только по зажатой ПКМ.
    void RecaptureMouseForLook();
    bool UsesUiPointer() const;
    bool BlocksGameMouseLook() const;
    bool TryRouteInGameOverlay(const GuiMouseEvent& event, bool pressed);
    bool ResolveSlotAt(int x, int y, SlotAddress& out);
    void DrawDragGhost(int width, int height);
    void ClearGameplayKeyboard();

    std::shared_ptr<Core> core_;
    std::shared_ptr<World> world_;
    std::shared_ptr<GeometryEngine> geometry_;
    std::shared_ptr<ViewEngine> views_;
    std::shared_ptr<TextRenderer> textRenderer_;
    std::shared_ptr<ShaderManager> shaderManager_;
    std::shared_ptr<BlockDefinitionStorage> blockDefinitions_;

    std::unique_ptr<GuiContext> guiContext_;
    std::unique_ptr<GameSession> gameSession_;

    AppState state_{AppState::MainMenu};
    UiSettings uiSettings_;
    GLFWwindow* window_{nullptr};
    bool consoleOpen_{false};
    bool paletteOpen_{false};
    bool freeCursor_{false};
    /// Подавить следующий glfw char после открытия консоли (символ клавиши-тоггла).
    bool suppressConsoleToggleChar_{false};
    enum class OverlayPointerCapture { None, Palette, Console, Hud };
    OverlayPointerCapture overlayPointerCapture_{OverlayPointerCapture::None};
    int dragCursorX_{0};
    int dragCursorY_{0};
    bool worldSessionActive_{false};
    bool pendingEnterGame_{false};
    bool pendingQuit_{false};
    std::function<void()> pendingMenuAction_;
    bool quitRequested_{false};

    std::unique_ptr<GuiIconSource> iconSource_;
    std::unique_ptr<InGameHudScreen> hudScreen_;
    std::unique_ptr<ConsoleScreen> consoleScreen_;
    std::unique_ptr<CreativePaletteScreen> paletteScreen_;
    std::unique_ptr<IGuiClipboard> clipboard_;
    std::unique_ptr<GuiPopupMenu> overlayPopup_;

    MenuSubview menuSubview_{MenuSubview::Main};
    MainMenuScreen* mainMenuScreen_{nullptr};
};

} // namespace cutum

#endif
