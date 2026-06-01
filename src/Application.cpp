#include "Application.h"

#include "BlockDefinitionStorage.h"
#include "Core.h"
#include "Game/GameSession.h"
#include "GeometryEngine.h"
#include "Gui/GuiContext.h"
#include "Gui/Screens/ConsoleScreen.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Screens/InGameHudScreen.h"
#include "Gui/Screens/MainMenuScreen.h"
#include "Gui/GuiTypes.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Prefab.h"
#include "ShaderManager.h"
#include "TextRenderer.h"
#include "ViewEngine.h"
#include "World.h"

#include <GLFW/glfw3.h>
#include <cctype>
#include <iostream>

namespace cutum {

namespace {

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
    if (core_ && blockDefinitions_) {
        const std::string typesPath = "content/types.json";
        gameSession_->InitializeCatalog(typesPath, *blockDefinitions_,
                                      *core_->GetPrefabLibrary());
    }
    gameSession_->RegisterCommands();
    state_ = AppState::MainMenu;
    ShowMainMenu();
}

void Application::RequestEnterGame()
{
    if (state_ == AppState::InGame) {
        return;
    }
    state_ = AppState::Loading;
    if (core_) {
        core_->EnterGame();
    }
    state_ = AppState::InGame;
    ShowInGameHud();
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
    guiContext_->SetScreen(std::make_unique<MainMenuScreen>(gameSession_.get()));
}

void Application::ShowInGameHud()
{
    auto hud = std::make_unique<InGameHudScreen>(gameSession_.get(), &guiContext_->GetTheme());
    hud->Build(*guiContext_);
    hudScreen_ = std::move(hud);

    consoleScreen_ = std::make_unique<ConsoleScreen>(gameSession_.get());
    consoleScreen_->Build(*guiContext_);
    consoleScreen_->SetVisible(false);

    paletteScreen_ =
        std::make_unique<CreativePaletteScreen>(&gameSession_->GetContentCatalog(), gameSession_.get());
    paletteScreen_->Build(*guiContext_);
    paletteScreen_->SetVisible(false);

    guiContext_->SetScreen(nullptr);
}

void Application::SetCursorForUi(bool uiMode)
{
    if (!window_) {
        return;
    }
    glfwSetInputMode(window_, GLFW_CURSOR, uiMode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

void Application::Update(double dt)
{
    if (state_ == AppState::MainMenu) {
        guiContext_->Update(dt);
    } else if (state_ == AppState::InGame) {
        if (hudScreen_) {
            hudScreen_->Update(dt);
        }
        if (consoleScreen_) {
            consoleScreen_->Update(dt);
        }
        if (paletteScreen_) {
            paletteScreen_->Update(dt);
        }
    }
    SetCursorForUi(WantsCaptureMouse() || state_ == AppState::MainMenu);
}

void Application::ProcessInput()
{
    (void)0;
}

void Application::RenderFrame(int width, int height, double viewDuration)
{
    if (state_ == AppState::MainMenu) {
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        guiContext_->Render(width, height);
        return;
    }

    if (state_ == AppState::InGame && geometry_ && views_) {
        geometry_->PrepareFrameRendering();
        const glm::vec4 clearColor = geometry_->GetSkyColor();
        glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        geometry_->Paint(width, height, viewDuration);
    }

    if (hudScreen_ && hudScreen_->GetRoot()) {
        guiContext_->RenderOverlay(*hudScreen_->GetRoot(), width, height);
    }
    if (consoleOpen_ && consoleScreen_ && consoleScreen_->GetRoot()) {
        guiContext_->RenderOverlay(*consoleScreen_->GetRoot(), width, height);
    }
    if (paletteOpen_ && paletteScreen_ && paletteScreen_->GetRoot()) {
        guiContext_->RenderOverlay(*paletteScreen_->GetRoot(), width, height);
    }
}

bool Application::WantsCaptureMouse() const
{
    if (state_ == AppState::MainMenu) {
        return true;
    }
    return consoleOpen_ || paletteOpen_ || guiContext_->WantsCaptureMouse();
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
    if (consoleOpen_ && key == GLFW_KEY_ENTER && consoleScreen_) {
      consoleScreen_->SubmitCommand();
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
        auto routeRoot = [&](GuiWidget* root) -> bool {
            if (!root) {
                return false;
            }
            GuiWidget* hit = root->HitTest(x, y);
            if (!hit) {
                return false;
            }
            return pressed ? hit->OnMouseDown(event) : hit->OnMouseUp(event);
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

    return pressed ? guiContext_->RouteMouseDown(event) : guiContext_->RouteMouseUp(event);
}

bool Application::RouteMouseMove(int x, int y)
{
    GuiMouseEvent event;
    event.x = x;
    event.y = y;
    if (state_ == AppState::InGame) {
        auto routeRoot = [&](GuiWidget* root) -> bool {
            return root && root->HitTest(x, y) && root->OnMouseMove(event);
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
    return guiContext_->RouteMouseMove(event);
}

bool Application::RouteScroll(double xoffset, double yoffset)
{
    return guiContext_->RouteScroll(GuiScrollEvent{xoffset, yoffset});
}

} // namespace cutum
