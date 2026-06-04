#include "Application.h"
#include "HotbarInput.h"
#include "SlotInteraction.h"

#include "CursorCapture.h"
#include "WindowManager.h"
#include "BlockDefinitionStorage.h"
#include "Core.h"
#include "Game/GameSession.h"
#include "GeometryEngine.h"
#include "Gui/GuiContext.h"
#include "Gui/GuiRenderer.h"
#include "Gui/Screens/ConsoleScreen.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Screens/InGameHudScreen.h"
#include "Gui/CreatureIconCache.h"
#include "Gui/GuiIconSource.h"
#include "Gui/PrefabIconCache.h"
#include "Gui/Screens/LoadWorldScreen.h"
#include "Gui/Screens/MainMenuScreen.h"
#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Screens/SettingsScreen.h"
#include "TextureCube.h"
#include "Gui/GuiTypes.h"
#include "Gui/Interfaces/IGuiIconSource.h"
#include "Gui/Interfaces/IGuiClipboard.h"
#include "Gui/Widgets/GuiPopupMenu.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Prefab.h"
#include "ShaderManager.h"
#include "TextRenderer.h"
#include "User.h"
#include "ViewEngine.h"
#include "World.h"
#include "Camera.h"
#include "CameraPerspective.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace cutum {

namespace {

WindowManager* GetWindowManager(GLFWwindow* window)
{
    if (!window) {
        return nullptr;
    }
    return static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
}

class GlfwClipboard : public IGuiClipboard {
public:
    explicit GlfwClipboard(GLFWwindow* window)
        : window_(window)
    {
    }

    std::string GetString() const override
    {
        if (!window_) {
            return {};
        }
        const char* text = glfwGetClipboardString(window_);
        return text ? std::string(text) : std::string{};
    }

    void SetString(const std::string& text) override
    {
        if (window_) {
            glfwSetClipboardString(window_, text.c_str());
        }
    }

private:
    GLFWwindow* window_;
};

bool KeyNameIs(const std::string& name, int glfwKey)
{
    if (name == "grave") {
        return glfwKey == GLFW_KEY_GRAVE_ACCENT;
    }
    if (name.size() == 1) {
        const char c = static_cast<char>(std::tolower(name[0]));
        if (c >= 'a' && c <= 'z') {
            return glfwKey == GLFW_KEY_A + (c - 'a');
        }
    }
    return false;
}

} // namespace

Application::Application(std::shared_ptr<Core> core,
                         std::shared_ptr<World> world,
                         std::shared_ptr<GeometryEngine> geometry,
                         std::shared_ptr<ViewEngine> views,
                         std::shared_ptr<TextRenderer> textRenderer,
                         std::shared_ptr<ShaderManager> shaderManager,
                         std::shared_ptr<BlockDefinitionStorage> blockDefinitions)
    : core_(std::move(core))
    , world_(std::move(world))
    , geometry_(std::move(geometry))
    , views_(std::move(views))
    , textRenderer_(std::move(textRenderer))
    , shaderManager_(std::move(shaderManager))
    , blockDefinitions_(std::move(blockDefinitions))
{
    guiContext_ = std::make_unique<GuiContext>();
    gameSession_ = std::make_unique<GameSession>(this, world_);
}

Application::~Application() = default;

void Application::Startup(const std::string& configPath)
{
    if (core_) {
        core_->LoadConfig(configPath);
        uiSettings_ = core_->GetUiSettings();
    }
    if (geometry_) {
        geometry_->SetShowHud(uiSettings_.legacyHud);
    }
    if (!guiContext_->Initialize(shaderManager_, textRenderer_)) {
        std::cerr << "Application: GuiContext init failed" << std::endl;
        return;
    }
    if (window_) {
        int fbW = 0;
        int fbH = 0;
        glfwGetFramebufferSize(window_, &fbW, &fbH);
        if (fbW > 0 && fbH > 0 && textRenderer_) {
            textRenderer_->SetWindowSize(fbW, fbH);
        }
    }
    if (core_ && blockDefinitions_) {
        const std::string typesPath = "content/types.json";
        gameSession_->InitializeCatalog(typesPath, *blockDefinitions_,
                                      *core_->GetPrefabLibrary());
    }
    gameSession_->RegisterCommands();

    if (core_ && blockDefinitions_ && shaderManager_) {
        auto textures = core_->GetTextureCubeStorage();
        auto prefabCache = std::make_unique<PrefabIconCache>(
            core_->GetPrefabLibrary(), textures, blockDefinitions_, shaderManager_);
        if (prefabCache->Initialize()) {
            std::unique_ptr<CreatureIconCache> creatureCache;
            if (world_) {
                creatureCache = std::make_unique<CreatureIconCache>(
                    world_->GetCreatureDefinitionStorage(), world_->GetSkinDefinitionStorage(),
                    core_->GetCreatureTextureStorage(), shaderManager_);
                if (!creatureCache->Initialize()) {
                    creatureCache.reset();
                }
            }
            iconSource_ = std::make_unique<GuiIconSource>(textures, std::move(prefabCache),
                                                          std::move(creatureCache));
        }
    }

    state_ = AppState::MainMenu;
    ShowMainMenu();
}

void Application::ScheduleEnterGame()
{
    pendingEnterGame_ = true;
}

void Application::ScheduleQuit()
{
    pendingQuit_ = true;
}

void Application::RequestEnterGame()
{
    if (state_ == AppState::InGame) {
        return;
    }

    if (!worldSessionActive_) {
        state_ = AppState::Loading;
        if (core_) {
            core_->EnterGame();
        }
        if (world_) {
            if (auto user = world_->GetCurrentUser()) {
                world_->EnsurePlayerHotbarCount(user, static_cast<size_t>(uiSettings_.hotbarCount));
            }
        }
        worldSessionActive_ = true;
        ShowInGameHud();
    } else {
        guiContext_->SetScreen(nullptr);
    }

    state_ = AppState::InGame;
    EnterInGameInputState();
}

void Application::RequestQuit()
{
    if (gameSession_) {
        gameSession_->SaveCommandHistory();
    }
    quitRequested_ = true;
    if (window_) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

void Application::ShowMainMenu()
{
    consoleOpen_ = false;
    suppressConsoleToggleChar_ = false;
    paletteOpen_ = false;
    freeCursor_ = false;
    auto menu = std::make_unique<MainMenuScreen>(gameSession_.get());
    mainMenuScreen_ = menu.get();
    menuSubview_ = MenuSubview::Main;
    guiContext_->SetScreen(std::move(menu));
    SyncCursorVisibility();
}

void Application::SetHotbarCountSetting(int count)
{
    uiSettings_.hotbarCount = std::clamp(count, 1, 2);
    if (core_) {
        AppSettingsSnapshot app = core_->GetAppSettings();
        app.ui = uiSettings_;
        core_->ApplyAppSettings(app);
        core_->SaveConfigFile();
    }
    if (world_) {
        if (auto user = world_->GetCurrentUser()) {
            world_->EnsurePlayerHotbarCount(user, static_cast<size_t>(uiSettings_.hotbarCount));
        }
    }
}

void Application::ReturnToMainMenu()
{
    if (state_ == AppState::InGame) {
        if (gameSession_) {
            gameSession_->SaveCommandHistory();
        }
        if (auto* wm = GetWindowManager(window_)) {
            wm->ResetGameplayMouseCapture();
        }
        guiContext_->ClearInputState();
        ReleasePlatformCursorClip();
    }
    consoleOpen_ = false;
    suppressConsoleToggleChar_ = false;
    paletteOpen_ = false;
    freeCursor_ = false;
    state_ = AppState::MainMenu;
    ShowMainMenu();
}

void Application::ShowSettings()
{
    consoleOpen_ = false;
    suppressConsoleToggleChar_ = false;
    paletteOpen_ = false;
    freeCursor_ = false;
    mainMenuScreen_ = nullptr;
    menuSubview_ = MenuSubview::Settings;
    guiContext_->SetScreen(std::make_unique<SettingsScreen>(this));
    SyncCursorVisibility();
}

void Application::ShowNewWorld()
{
    consoleOpen_ = false;
    suppressConsoleToggleChar_ = false;
    paletteOpen_ = false;
    freeCursor_ = false;
    mainMenuScreen_ = nullptr;
    menuSubview_ = MenuSubview::NewWorld;
    guiContext_->SetScreen(std::make_unique<NewWorldScreen>(this));
    SyncCursorVisibility();
}

void Application::ShowLoadWorld()
{
    consoleOpen_ = false;
    suppressConsoleToggleChar_ = false;
    paletteOpen_ = false;
    freeCursor_ = false;
    mainMenuScreen_ = nullptr;
    menuSubview_ = MenuSubview::LoadWorld;
    guiContext_->SetScreen(std::make_unique<LoadWorldScreen>(this));
    SyncCursorVisibility();
}

void Application::SaveActiveWorldIfNeeded()
{
    if (!worldSessionActive_ || !core_) {
        return;
    }
    core_->SaveWorld(world_->GetWorldName());
    core_->SaveConfigFile();
}

void Application::EnterGameAfterWorldChange()
{
    worldSessionActive_ = true;
    if (core_ && world_) {
        uiSettings_ = core_->GetUiSettings();
        if (auto user = world_->GetCurrentUser()) {
            world_->EnsurePlayerHotbarCount(user, static_cast<size_t>(uiSettings_.hotbarCount));
        }
        if (geometry_) {
            geometry_->SetShowHud(uiSettings_.legacyHud);
        }
        world_->FinalizePlayerAfterWorldLoad();
    }
    ShowInGameHud();
    state_ = AppState::InGame;
    EnterInGameInputState();
}

void Application::ScheduleDeferredMenuAction(std::function<void()> action)
{
    pendingMenuAction_ = std::move(action);
}

void Application::SaveIfNeededAndProceed(std::function<void()> proceed)
{
    if (!proceed) {
        return;
    }
    SaveActiveWorldIfNeeded();
    ScheduleDeferredMenuAction(std::move(proceed));
}

AppSettingsSnapshot Application::LoadAppSettingsSnapshot() const
{
    return core_ ? core_->GetAppSettings() : AppSettingsSnapshot{};
}

ProceduralSettings Application::LoadProceduralTemplate() const
{
    return core_ ? core_->GetProceduralTemplate() : ProceduralSettings{};
}

void Application::SaveAppAndTemplateSettings(const AppSettingsSnapshot& app,
                                             const ProceduralSettings& procedural)
{
    if (!core_) {
        return;
    }
    core_->ApplyAppSettings(app);
    core_->SetProceduralTemplate(procedural);
    core_->SaveConfigFile();
    uiSettings_ = core_->GetUiSettings();
    if (geometry_) {
        geometry_->SetShowHud(uiSettings_.legacyHud);
    }
    if (world_) {
        if (auto user = world_->GetCurrentUser()) {
            world_->EnsurePlayerHotbarCount(user, static_cast<size_t>(uiSettings_.hotbarCount));
        }
    }
    SyncCursorVisibility();
}

void Application::CreateNewWorldWithSettings(const ProceduralSettings& settings)
{
    if (!core_) {
        return;
    }
    core_->SetProceduralTemplate(settings);
    core_->CreateNewWorldFromTemplate();
    core_->SaveConfigFile();
    EnterGameAfterWorldChange();
}

void Application::LoadSelectedWorld(const std::string& worldName)
{
    if (!core_) {
        return;
    }
    core_->LoadWorldByName(worldName);
    core_->SaveConfigFile();
    EnterGameAfterWorldChange();
}

void Application::RefreshWorldList()
{
    if (core_) {
        core_->RefreshWorldList();
    }
}

const std::vector<std::string>& Application::GetWorldNames() const
{
    static const std::vector<std::string> kEmpty;
    return core_ ? core_->GetWorldList() : kEmpty;
}

void Application::ShowInGameHud()
{
    if (window_ && !clipboard_) {
        clipboard_ = std::make_unique<GlfwClipboard>(window_);
        guiContext_->SetClipboard(clipboard_.get());
    }

    gameSession_->InitCommandHistory(GetExecutableDirectory() / "console_history.txt");

    IGuiIconSource* icons = iconSource_.get();
    auto hud = std::make_unique<InGameHudScreen>(gameSession_.get(), &guiContext_->GetTheme(), icons);
    hud->Build(*guiContext_);
    hudScreen_ = std::move(hud);

    overlayPopup_ = std::make_unique<GuiPopupMenu>(&guiContext_->GetTheme());

    consoleScreen_ = std::make_unique<ConsoleScreen>(gameSession_.get());
    consoleScreen_->Build(*guiContext_);
    consoleScreen_->AttachPopup(overlayPopup_.get());
    consoleScreen_->SetVisible(false);

    paletteScreen_ = std::make_unique<CreativePaletteScreen>(
        &gameSession_->GetContentCatalog(), gameSession_.get(), icons);
    paletteScreen_->Build(*guiContext_);
    paletteScreen_->SetVisible(false);

    guiContext_->SetScreen(nullptr);
}

bool Application::UsesUiPointer() const
{
    return state_ == AppState::MainMenu || freeCursor_ || consoleOpen_ || paletteOpen_;
}

bool Application::BlocksGameMouseLook() const
{
    return state_ == AppState::InGame && (freeCursor_ || consoleOpen_ || paletteOpen_);
}

AppCursorPolicy Application::GetCursorPolicy() const
{
    if (UsesUiPointer()) {
        return AppCursorPolicy::Free;
    }
    if (state_ == AppState::InGame) {
        if (uiSettings_.controlScheme == ControlScheme::Classic) {
            return AppCursorPolicy::CapturedHidden;
        }
        return AppCursorPolicy::ConfinedVisible;
    }
    return AppCursorPolicy::Free;
}

void Application::SyncCursorVisibility()
{
    if (!window_) {
        return;
    }
    ApplyCursorPolicy(window_, GetCursorPolicy());
}

void Application::ClearGameplayKeyboard()
{
    if (!world_) {
        return;
    }
    if (auto camera = world_->GetCurrentUserCamera()) {
        camera->ResetAllKeyStatus();
    }
}

void Application::HandleWindowFocus(bool focused)
{
    if (!window_) {
        return;
    }
    if (!focused) {
        ReleasePlatformCursorClip();
        return;
    }
    SyncCursorVisibility();
}

void Application::EnterInGameInputState()
{
    consoleOpen_ = false;
    suppressConsoleToggleChar_ = false;
    paletteOpen_ = false;
    freeCursor_ = false;
    guiContext_->ClearInputState();
    SyncCursorVisibility();
    if (auto* wm = GetWindowManager(window_)) {
        wm->ResetGameplayMouseCapture();
    }
    if (world_ && window_) {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window_, &x, &y);
        if (auto camera = world_->GetCurrentUserCamera()) {
            camera->ResetMouseMove(x, y);
        }
    }
}

void Application::RecaptureMouseForLook()
{
    freeCursor_ = false;
    EnterInGameInputState();
}

bool Application::TryRouteInGameOverlay(const GuiMouseEvent& event, bool pressed)
{
    auto routeRoot = [&](GuiWidget* root, bool requireHitTest) -> bool {
        if (!root) {
            return false;
        }
        if (requireHitTest && !root->HitTest(event.x, event.y)) {
            return false;
        }
        return pressed ? root->OnMouseDown(event) : root->OnMouseUp(event);
    };

    if (pressed) {
        overlayPointerCapture_ = OverlayPointerCapture::None;
        if (paletteOpen_ && routeRoot(paletteScreen_->GetRoot(), true)) {
            overlayPointerCapture_ = OverlayPointerCapture::Palette;
            return true;
        }
        if (consoleOpen_ && routeRoot(consoleScreen_->GetRoot(), true)) {
            overlayPointerCapture_ = OverlayPointerCapture::Console;
            return true;
        }
        if (routeRoot(hudScreen_ ? hudScreen_->GetRoot() : nullptr, true)) {
            overlayPointerCapture_ = OverlayPointerCapture::Hud;
            return true;
        }
        return false;
    }

    const OverlayPointerCapture capture = overlayPointerCapture_;
    overlayPointerCapture_ = OverlayPointerCapture::None;
    if (capture == OverlayPointerCapture::None) {
        return false;
    }

    switch (capture) {
    case OverlayPointerCapture::Palette:
        return paletteOpen_ && routeRoot(paletteScreen_->GetRoot(), false);
    case OverlayPointerCapture::Console:
        return consoleOpen_ && routeRoot(consoleScreen_->GetRoot(), false);
    case OverlayPointerCapture::Hud:
        return routeRoot(hudScreen_ ? hudScreen_->GetRoot() : nullptr, false);
    default:
        return false;
    }
}

bool Application::ResolveSlotAt(int x, int y, SlotAddress& out)
{
    // Хотбар под палитрой: при drop сначала проверяем HUD, иначе палитра «съедает» цель.
    if (hudScreen_ && hudScreen_->PickSlot(x, y, out)) {
        return true;
    }
    if (paletteOpen_ && paletteScreen_ && paletteScreen_->PickSlot(x, y, out)) {
        return true;
    }
    return false;
}

void Application::DrawDragGhost(int width, int height)
{
    if (!gameSession_ || !gameSession_->IsDragging() || !iconSource_ || !guiContext_) {
        return;
    }
    const DragState& drag = gameSession_->GetDrag();
    if (drag.entry.empty) {
        return;
    }
    GLuint tex = 0;
    switch (drag.entry.kind) {
    case InventoryEntryKind::Block:
        tex = iconSource_->GetBlockIconTexture(drag.entry.id);
        break;
    case InventoryEntryKind::Object:
        tex = iconSource_->GetPrefabIconTexture(drag.entry.id);
        break;
    case InventoryEntryKind::Creature:
        tex = iconSource_->GetCreatureIconTexture(drag.entry.id);
        break;
    case InventoryEntryKind::Skin:
        tex = iconSource_->GetSkinIconTexture(drag.entry.id);
        break;
    }
    if (tex == 0) {
        return;
    }
    const int size = guiContext_->GetTheme().hotbarSlotSize;
    const GuiRect rect{dragCursorX_ - size / 2, dragCursorY_ - size / 2, size, size};
    GuiRenderer& renderer = guiContext_->GetRenderer();
    renderer.BeginFrame(width, height);
    renderer.DrawTexturedRect(rect, tex);
    renderer.EndFrame();
}

void Application::Update(double dt)
{
    if (pendingEnterGame_) {
        pendingEnterGame_ = false;
        RequestEnterGame();
    }
    if (pendingQuit_) {
        pendingQuit_ = false;
        RequestQuit();
    }
    if (pendingMenuAction_) {
        auto action = std::move(pendingMenuAction_);
        pendingMenuAction_ = nullptr;
        action();
    }

    if (state_ == AppState::MainMenu) {
        guiContext_->Update(dt);
    } else if (state_ == AppState::InGame) {
        if (hudScreen_ && window_) {
            int fbW = 0;
            int fbH = 0;
            glfwGetFramebufferSize(window_, &fbW, &fbH);
            if (fbW > 0 && fbH > 0) {
                hudScreen_->OnViewportChanged(fbW, fbH);
            }
            hudScreen_->Update(dt);
        }
        if (consoleScreen_) {
            consoleScreen_->Update(dt);
        }
        if (paletteScreen_) {
            paletteScreen_->Update(dt);
        }
    }
    SyncCursorVisibility();
}

void Application::ProcessInput()
{
    (void)0;
}

void Application::RenderFrame(int width, int height, double viewDuration)
{
    if (state_ == AppState::MainMenu) {
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        guiContext_->Render(width, height);
        return;
    }

    if (state_ == AppState::InGame && geometry_ && world_) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        if (auto camera = world_->GetCurrentUserCamera()) {
            const float aspect =
                static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1);
            camera->SetAspectRatio(aspect);
        }
        geometry_->PrepareFrameRendering();
        const glm::vec4 clearColor = geometry_->GetSkyColor();
        glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        geometry_->Paint(width, height, viewDuration);
    }

    if (state_ == AppState::InGame && iconSource_) {
        iconSource_->WarmupPrefabIcons(2);
        iconSource_->WarmupCreatureIcons(2);
    }

    if (hudScreen_ && hudScreen_->GetRoot()) {
        hudScreen_->SyncSlotIcons();
        hudScreen_->OnViewportChanged(width, height);
        guiContext_->RenderOverlay(*hudScreen_->GetRoot(), width, height, false);
    }
    if (consoleOpen_ && consoleScreen_ && consoleScreen_->GetRoot()) {
        consoleScreen_->OnViewportChanged(width, height);
        guiContext_->RenderOverlay(*consoleScreen_->GetRoot(), width, height, false);
    }
    if (overlayPopup_ && overlayPopup_->IsOpen()) {
        auto& renderer = guiContext_->GetRenderer();
        renderer.BeginFrame(width, height);
        overlayPopup_->Draw(renderer);
        renderer.EndFrame();
    }
    if (paletteOpen_ && paletteScreen_ && paletteScreen_->GetRoot()) {
        paletteScreen_->OnViewportChanged(width, height);
        guiContext_->RenderOverlay(*paletteScreen_->GetRoot(), width, height, false);
    }
    if (state_ == AppState::InGame) {
        DrawDragGhost(width, height);
    }
}

bool Application::WantsCaptureMouse() const
{
    if (state_ == AppState::MainMenu) {
        return true;
    }
    if (BlocksGameMouseLook()) {
        return true;
    }
    return guiContext_->WantsCaptureMouse();
}

bool Application::WantsCaptureKeyboard() const
{
    if (state_ == AppState::MainMenu) {
        return true;
    }
    return consoleOpen_ || paletteOpen_ || guiContext_->WantsCaptureKeyboard();
}

bool Application::AllowsWorldMousePlacement() const
{
    return state_ == AppState::InGame && !paletteOpen_ && !consoleOpen_;
}

bool Application::RouteKey(int key, int action, int mods)
{
  GuiKeyEvent event;
  event.keyCode = key;
  event.action = action == GLFW_REPEAT ? GuiKeyAction::Repeat
                 : (action == GLFW_PRESS ? GuiKeyAction::Press : GuiKeyAction::Release);
  event.mods = mods;

    if (action == GLFW_PRESS && state_ == AppState::InGame) {
    if (key == GLFW_KEY_ESCAPE) {
      if (consoleOpen_ && consoleScreen_ && consoleScreen_->IsPopupOpen()) {
        if (overlayPopup_) {
          overlayPopup_->Close();
        }
        return true;
      }
      if (consoleOpen_) {
        consoleOpen_ = false;
        suppressConsoleToggleChar_ = false;
        if (consoleScreen_) {
          consoleScreen_->SetVisible(false);
        }
        ClearGameplayKeyboard();
        return true;
      }
      if (paletteOpen_) {
        paletteOpen_ = false;
        if (paletteScreen_) {
          paletteScreen_->SetVisible(false);
        }
        return true;
      }
      ReturnToMainMenu();
      return true;
    }
    if (!consoleOpen_ && key == GLFW_KEY_RIGHT_ALT) {
      freeCursor_ = !freeCursor_;
      if (freeCursor_) {
        if (auto* wm = GetWindowManager(window_)) {
          wm->ResetGameplayMouseCapture();
        }
        if (world_ && window_) {
          double x = 0.0;
          double y = 0.0;
          glfwGetCursorPos(window_, &x, &y);
          if (auto camera = world_->GetCurrentUserCamera()) {
            camera->ResetMouseMove(x, y);
          }
        }
      } else {
        RecaptureMouseForLook();
      }
      SyncCursorVisibility();
      return true;
    }
    if (KeyNameIs(uiSettings_.consoleKey, key)) {
      consoleOpen_ = !consoleOpen_;
      if (consoleScreen_) {
        consoleScreen_->SetVisible(consoleOpen_);
      }
      if (consoleOpen_) {
        ClearGameplayKeyboard();
        suppressConsoleToggleChar_ = true;
      } else {
        suppressConsoleToggleChar_ = false;
      }
      SyncCursorVisibility();
      return true;
    }
    if (!consoleOpen_ && key == GLFW_KEY_F5 && world_) {
      if (auto cam = world_->GetCurrentUserCamera()) {
        cam->CyclePerspective();
        if (geometry_) {
          geometry_->ShowTransientMessage(CameraPerspectiveLabel(cam->GetPerspective()), 1.5);
        }
      }
      return true;
    }
    if (!consoleOpen_ && KeyNameIs(uiSettings_.paletteKey, key)) {
      paletteOpen_ = !paletteOpen_;
      if (paletteScreen_) {
        paletteScreen_->SetVisible(paletteOpen_);
      }
      SyncCursorVisibility();
      return true;
    }
    if (!consoleOpen_ && KeyNameIs(uiSettings_.inventoryKey, key)) {
      paletteOpen_ = !paletteOpen_;
      if (paletteScreen_) {
        paletteScreen_->SetVisible(paletteOpen_);
      }
      SyncCursorVisibility();
      return true;
    }
    if (consoleOpen_ && key == GLFW_KEY_ENTER && consoleScreen_) {
      consoleScreen_->SubmitCommand();
      return true;
    }
    if (!consoleOpen_) {
      const int hotbarSlot = PrimaryHotbarIndexFromGlfwKey(key);
      if (hotbarSlot >= 0 && gameSession_) {
        gameSession_->OnPrimaryHotbarKey(hotbarSlot);
        return true;
      }
    }
  }

  if (action == GLFW_PRESS && state_ == AppState::MainMenu && key == GLFW_KEY_ESCAPE) {
    if (mainMenuScreen_ && mainMenuScreen_->IsQuitConfirmationVisible()) {
      mainMenuScreen_->ShowQuitConfirmation(false);
      return true;
    }
    if (menuSubview_ != MenuSubview::Main) {
      ShowMainMenu();
      return true;
    }
    if (HasWorldSession() && gameSession_) {
      gameSession_->ResumeGame();
      return true;
    }
    if (mainMenuScreen_) {
      mainMenuScreen_->ShowQuitConfirmation(true);
      return true;
    }
  }

  if (state_ == AppState::InGame && consoleOpen_ && consoleScreen_) {
    if (KeyNameIs(uiSettings_.consoleKey, key)) {
      return true;
    }
    consoleScreen_->RouteKey(event);
    return true;
  }

  if (guiContext_->RouteKey(event)) {
    return true;
  }
  return false;
}

bool Application::RouteChar(unsigned int codepoint)
{
    if (state_ == AppState::InGame && consoleOpen_ && consoleScreen_) {
        if (suppressConsoleToggleChar_) {
            suppressConsoleToggleChar_ = false;
            return true;
        }
        consoleScreen_->RouteChar(GuiCharEvent{codepoint});
        return true;
    }
    suppressConsoleToggleChar_ = false;
    return guiContext_->RouteChar(GuiCharEvent{codepoint});
}

bool Application::RouteMouseButton(int button, bool pressed, int x, int y)
{
    GuiMouseEvent event;
    event.x = x;
    event.y = y;
    event.button = button == GLFW_MOUSE_BUTTON_RIGHT   ? GuiMouseButton::Right
                   : button == GLFW_MOUSE_BUTTON_MIDDLE ? GuiMouseButton::Middle
                                                       : GuiMouseButton::Left;
    event.pressed = pressed;

    if (state_ == AppState::InGame) {
        dragCursorX_ = x;
        dragCursorY_ = y;
        if (event.button == GuiMouseButton::Left && !pressed && gameSession_ &&
            gameSession_->IsDragging()) {
            SlotAddress target;
            const bool hasTarget = ResolveSlotAt(x, y, target);
            if (hasTarget) {
                if (!gameSession_->DropOnSlot(target)) {
                    gameSession_->CancelDrag();
                }
            } else {
                gameSession_->CancelDrag();
            }
            if (overlayPointerCapture_ != OverlayPointerCapture::None) {
                TryRouteInGameOverlay(event, false);
            }
            return true;
        }
        if ((overlayPopup_ && overlayPopup_->IsOpen()) || consoleOpen_) {
            if (consoleScreen_ &&
                consoleScreen_->RouteMouseButton(event, guiContext_->GetRenderer())) {
                return true;
            }
        }
        if (event.button == GuiMouseButton::Left) {
            if (TryRouteInGameOverlay(event, pressed)) {
                return true;
            }
            if (freeCursor_ && pressed) {
                RecaptureMouseForLook();
                return true;
            }
        }
        return false;
    }

    return pressed ? guiContext_->RouteMouseDown(event) : guiContext_->RouteMouseUp(event);
}

bool Application::RouteMouseMove(int x, int y)
{
    GuiMouseEvent event;
    event.x = x;
    event.y = y;
    if (state_ == AppState::InGame && hudScreen_) {
        hudScreen_->SetPointerPosition(x, y);
    }
    if (state_ == AppState::InGame) {
        dragCursorX_ = x;
        dragCursorY_ = y;
        if (gameSession_ && gameSession_->IsDragging()) {
            return true;
        }
        if (overlayPopup_ && overlayPopup_->IsOpen()) {
            if (overlayPopup_->OnMouseMove(event)) {
                return true;
            }
        }
        if (consoleOpen_ && consoleScreen_ &&
            consoleScreen_->RouteMouseMove(event, guiContext_->GetRenderer())) {
            return true;
        }
        auto routeMove = [&](GuiWidget* root) -> bool {
            return root && root->OnMouseMove(event);
        };
        bool handled = false;
        if (paletteOpen_) {
            handled |= routeMove(paletteScreen_->GetRoot());
        }
        handled |= routeMove(hudScreen_ ? hudScreen_->GetRoot() : nullptr);
        return handled;
    }
    return guiContext_->RouteMouseMove(event);
}

bool Application::RouteScroll(double xoffset, double yoffset, int mouseX, int mouseY)
{
    if (state_ == AppState::InGame) {
        GuiScrollEvent event{xoffset, yoffset};
        auto routeScroll = [&](GuiWidget* root) -> bool {
            return root && root->ScrollAtPoint(mouseX, mouseY, event);
        };
        if (paletteOpen_ && routeScroll(paletteScreen_ ? paletteScreen_->GetRoot() : nullptr)) {
            return true;
        }
        if (consoleOpen_ && routeScroll(consoleScreen_ ? consoleScreen_->GetRoot() : nullptr)) {
            return true;
        }
        if (routeScroll(hudScreen_ ? hudScreen_->GetRoot() : nullptr)) {
            return true;
        }
        return false;
    }
    return guiContext_->RouteScroll(GuiScrollEvent{xoffset, yoffset}, mouseX, mouseY);
}

} // namespace cutum
