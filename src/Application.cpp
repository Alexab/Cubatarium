#include "Application.h"

#include "CursorCapture.h"
#include "WindowManager.h"
#include "BlockDefinitionStorage.h"
#include "Core.h"
#include "Game/GameSession.h"
#include "GeometryEngine.h"
#include "Gui/GuiContext.h"
#include "Gui/Screens/ConsoleScreen.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Screens/InGameHudScreen.h"
#include "Gui/GuiIconSource.h"
#include "Gui/PrefabIconCache.h"
#include "Gui/Screens/LoadWorldScreen.h"
#include "Gui/Screens/MainMenuScreen.h"
#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Screens/SettingsScreen.h"
#include "TextureCube.h"
#include "Gui/GuiTypes.h"
#include "Gui/Interfaces/IGuiIconSource.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Prefab.h"
#include "ShaderManager.h"
#include "TextRenderer.h"
#include "User.h"
#include "ViewEngine.h"
#include "World.h"

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
            iconSource_ = std::make_unique<GuiIconSource>(textures, std::move(prefabCache));
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
                user->EnsureHotbarCount(static_cast<size_t>(uiSettings_.hotbarCount));
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
    quitRequested_ = true;
    if (window_) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

void Application::ShowMainMenu()
{
    consoleOpen_ = false;
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
            user->EnsureHotbarCount(static_cast<size_t>(uiSettings_.hotbarCount));
        }
    }
}

void Application::ReturnToMainMenu()
{
    if (state_ == AppState::InGame) {
        if (auto* wm = GetWindowManager(window_)) {
            wm->ResetGameplayMouseCapture();
        }
        guiContext_->ClearInputState();
        ReleasePlatformCursorClip();
    }
    consoleOpen_ = false;
    paletteOpen_ = false;
    freeCursor_ = false;
    state_ = AppState::MainMenu;
    ShowMainMenu();
}

void Application::ShowSettings()
{
    consoleOpen_ = false;
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
            user->EnsureHotbarCount(static_cast<size_t>(uiSettings_.hotbarCount));
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
            user->EnsureHotbarCount(static_cast<size_t>(uiSettings_.hotbarCount));
        }
    }
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
    IGuiIconSource* icons = iconSource_.get();
    auto hud = std::make_unique<InGameHudScreen>(gameSession_.get(), &guiContext_->GetTheme(), icons);
    hud->Build(*guiContext_);
    hudScreen_ = std::move(hud);

    consoleScreen_ = std::make_unique<ConsoleScreen>(gameSession_.get());
    consoleScreen_->Build(*guiContext_);
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
    auto routeRoot = [&](GuiWidget* root) -> bool {
        if (!root) {
            return false;
        }
        if (!root->HitTest(event.x, event.y)) {
            return false;
        }
        return pressed ? root->OnMouseDown(event) : root->OnMouseUp(event);
    };
    if (paletteOpen_ && routeRoot(paletteScreen_->GetRoot())) {
        return true;
    }
    if (consoleOpen_ && routeRoot(consoleScreen_->GetRoot())) {
        return true;
    }
    if (routeRoot(hudScreen_ ? hudScreen_->GetRoot() : nullptr)) {
        return true;
    }
    return false;
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
        if (paletteOpen_ && paletteScreen_) {
            paletteScreen_->InvalidateGrid();
        }
    }

    if (hudScreen_ && hudScreen_->GetRoot()) {
        hudScreen_->SyncSlotIcons();
        hudScreen_->OnViewportChanged(width, height);
        guiContext_->RenderOverlay(*hudScreen_->GetRoot(), width, height, false);
    }
    if (consoleOpen_ && consoleScreen_ && consoleScreen_->GetRoot()) {
        consoleScreen_->OnViewportChanged(width, height);
        guiContext_->RenderOverlay(*consoleScreen_->GetRoot(), width, height);
    }
    if (paletteOpen_ && paletteScreen_ && paletteScreen_->GetRoot()) {
        paletteScreen_->OnViewportChanged(width, height);
        guiContext_->RenderOverlay(*paletteScreen_->GetRoot(), width, height, false);
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

bool Application::RouteKey(int key, int action, int mods)
{
  GuiKeyEvent event;
  event.keyCode = key;
  event.action = action == GLFW_REPEAT ? GuiKeyAction::Repeat
                 : (action == GLFW_PRESS ? GuiKeyAction::Press : GuiKeyAction::Release);
  event.mods = mods;

  if (action == GLFW_PRESS && state_ == AppState::InGame) {
    if (key == GLFW_KEY_ESCAPE) {
      if (consoleOpen_) {
        consoleOpen_ = false;
        if (consoleScreen_) {
          consoleScreen_->SetVisible(false);
        }
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
    if (key == GLFW_KEY_RIGHT_ALT) {
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
      return true;
    }
    if (KeyNameIs(uiSettings_.paletteKey, key)) {
      paletteOpen_ = !paletteOpen_;
      if (paletteScreen_) {
        paletteScreen_->SetVisible(paletteOpen_);
      }
      return true;
    }
    if (KeyNameIs(uiSettings_.inventoryKey, key)) {
      paletteOpen_ = !paletteOpen_;
      if (paletteScreen_) {
        paletteScreen_->SetVisible(paletteOpen_);
      }
      return true;
    }
    if (consoleOpen_ && key == GLFW_KEY_ENTER && consoleScreen_) {
      consoleScreen_->SubmitCommand();
      return true;
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

  if (guiContext_->RouteKey(event)) {
    return true;
  }
  return false;
}

bool Application::RouteChar(unsigned int codepoint)
{
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
        auto routeMove = [&](GuiWidget* root) -> bool {
            return root && root->HitTest(x, y) && root->OnMouseMove(event);
        };
        if (paletteOpen_ && routeMove(paletteScreen_->GetRoot())) {
            return true;
        }
        if (consoleOpen_ && routeMove(consoleScreen_->GetRoot())) {
            return true;
        }
        if (routeMove(hudScreen_ ? hudScreen_->GetRoot() : nullptr)) {
            return true;
        }
        return false;
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
