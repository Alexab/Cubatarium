#include "WindowManager.h"
#include "Application.h"
#include "InputManager.h"
#include "Core.h"
#include "ProceduralSettings.h"
#include "World.h"
#include "Creature.h"
#include "CreatureInventory.h"
#include "GeometryEngine.h"
#include "ViewEngine.h"
#include "User.h"
#include "InventoryTypes.h"
#include "AppState.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace cutum {

namespace {

glm::ivec2 CursorToFramebufferPixels(GLFWwindow* window, float x, float y)
{
    if (!window) {
        return {static_cast<int>(x), static_cast<int>(y)};
    }
    int fbW = 0;
    int fbH = 0;
    int winW = 0;
    int winH = 0;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glfwGetWindowSize(window, &winW, &winH);
    if (winW <= 0 || winH <= 0 || fbW <= 0 || fbH <= 0) {
        return {static_cast<int>(x), static_cast<int>(y)};
    }
    const float sx = static_cast<float>(fbW) / static_cast<float>(winW);
    const float sy = static_cast<float>(fbH) / static_cast<float>(winH);
    return {static_cast<int>(x * sx), static_cast<int>(y * sy)};
}

} // namespace

WindowManager::WindowManager()
    : window(nullptr)
    , windowWidth(1280)
    , windowHeight(720)
    , isRunning(false)
    , isInitialized(false)
    , deltaTime(0.0)
    , isMousePressed(false)
    , isLeftMouseButtonPressed(false)
    , skyColor(0.5f, 0.7f, 1.0f, 1.0f)
    , useGradientSky(false)
{
    lastFrameTime = std::chrono::high_resolution_clock::now();
    lastAutosaveTime_ = std::chrono::steady_clock::now();
}

WindowManager::~WindowManager() {
    Shutdown();
}

bool WindowManager::Initialize(int width, int height, const char* title) {
    // GLFW initialization
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // GLFW configuration
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4); // MSAA

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // Window creation
    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    windowWidth = width;
    windowHeight = height;

    // OpenGL context creation
    glfwMakeContextCurrent(window);

    // GLEW initialization (must be after context creation)
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return false;
    }

    // Настройка OpenGL
    InitializeOpenGL();

    inputManager = std::make_shared<InputManager>();
    
    // TextRenderer will be set later via SetTextRenderer
    
    // Настройка callbacks
    SetupCallbacks();

    // Input manager creation
    inputManager->Initialize(window);

    isInitialized = true;
    return true;
}

void WindowManager::InitializeOpenGL() {
    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Enable MSAA
    glEnable(GL_MULTISAMPLE);

    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Set clear color (sky)
    glClearColor(skyColor.r, skyColor.g, skyColor.b, skyColor.a);

    // Viewport configuration
    glViewport(0, 0, windowWidth, windowHeight);
}

void WindowManager::SetupCallbacks() {
    // Set callback functions
    glfwSetFramebufferSizeCallback(window, InputManager::GLFWFramebufferSizeCallback);
    glfwSetKeyCallback(window, InputManager::GLFWKeyCallback);
    glfwSetMouseButtonCallback(window, InputManager::GLFWMouseButtonCallback);
    glfwSetCursorPosCallback(window, InputManager::GLFWCursorPosCallback);
    glfwSetScrollCallback(window, InputManager::GLFWScrollCallback);
    glfwSetErrorCallback(ErrorCallback);
    glfwSetWindowCloseCallback(window, WindowCloseCallback);
    glfwSetWindowUserPointer(window, this);

    glfwSetWindowFocusCallback(window, [](GLFWwindow* win, int focused) {
        auto* self = static_cast<WindowManager*>(glfwGetWindowUserPointer(win));
        if (self && self->application_) {
            self->application_->HandleWindowFocus(focused == GLFW_TRUE);
        }
    });

    // Configure callbacks for InputManager
    inputManager->SetKeyCallback([this](KeyCode key, KeyState state, int mods) {
        HandleKeyEvent(key, state, mods);
    });

    inputManager->SetMouseButtonCallback([this](MouseButton button, bool pressed, glm::vec2 pos) {
        HandleMouseButtonEvent(button, pressed, pos);
    });

    inputManager->SetMouseMoveCallback([this](glm::vec2 pos, glm::vec2 delta) {
        HandleMouseMoveEvent(pos, delta);
    });

    inputManager->SetWindowResizeCallback([this](int width, int height) {
        HandleWindowResizeEvent(width, height);
    });

    inputManager->SetMouseScrollCallback([this](double xoffset, double yoffset) {
        if (!application_) {
            return;
        }
        const glm::vec2 pos = inputManager->GetMousePosition();
        const glm::ivec2 fbPos = CursorToFramebufferPixels(window, pos.x, pos.y);
        if (application_->RouteScroll(xoffset, yoffset, fbPos.x, fbPos.y)) {
            return;
        }
    });

    glfwSetCharCallback(window, [](GLFWwindow* win, unsigned int codepoint) {
        auto* self = static_cast<WindowManager*>(glfwGetWindowUserPointer(win));
        if (self && self->application_) {
            self->application_->RouteChar(codepoint);
        }
    });
}

void WindowManager::Run() {
    if (!isInitialized) {
        std::cerr << "WindowManager not initialized" << std::endl;
        return;
    }

    isRunning = true;

    while (!glfwWindowShouldClose(window) && isRunning) {
        // Time update
        auto currentTime = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<double>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;

        glfwPollEvents();

        // Input processing
        ProcessInput();
        if (application_) {
            application_->Update(deltaTime);
        }

        // Logic update
        Update();

        // Rendering
        Render();

        glfwSwapBuffers(window);
    }
}

void WindowManager::ProcessInput() {
    inputManager->Update();

    if (application_ && application_->WantsCaptureKeyboard()) {
        return;
    }

    // Camera control key processing
    if (worldInstance && application_ && application_->GetState() == AppState::InGame) {
        auto camera = worldInstance->GetCurrentUserCamera();
        if (camera) {
            const bool shiftDown = inputManager->IsKeyPressed(KeyCode::Key_Shift)
                || (window && glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
            camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_W),
                                  inputManager->IsKeyPressed(KeyCode::Key_W));
            camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_S),
                                  inputManager->IsKeyPressed(KeyCode::Key_S));
            camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_A),
                                  inputManager->IsKeyPressed(KeyCode::Key_A));
            camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_D),
                                  inputManager->IsKeyPressed(KeyCode::Key_D));
            camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_Space),
                                  inputManager->IsKeyPressed(KeyCode::Key_Space));
            camera->UpdateKeyStatus(GLFW_KEY_LEFT_SHIFT, shiftDown);
            camera->UpdateKeyStatus(GLFW_KEY_RIGHT_SHIFT, shiftDown);
        }
    }
}

void WindowManager::Update() {
    if (views) {
        views->UpdateFrameTime();
    }

    if (worldInstance && application_ && application_->GetState() == AppState::InGame) {
        worldInstance->DoMovement();
    }

    if (core && worldInstance && application_ && application_->GetState() == AppState::InGame) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - lastAutosaveTime_).count();
        if (elapsed >= kAutosaveIntervalSec) {
            core->SaveWorld(worldInstance->GetWorldName());
            lastAutosaveTime_ = now;
        }
    }
}

void WindowManager::Render() {
    if (application_) {
        application_->SetWindow(window);
        int fbW = windowWidth;
        int fbH = windowHeight;
        if (window) {
            glfwGetFramebufferSize(window, &fbW, &fbH);
            if (fbW > 0 && fbH > 0) {
                windowWidth = fbW;
                windowHeight = fbH;
            }
        }
        application_->RenderFrame(fbW, fbH, views ? views->GetDurationUpdateMks() : 0.0);
        return;
    }

    if (geometries) {
        geometries->PrepareFrameRendering();
        const glm::vec4 clearColor = geometries->GetSkyColor();
        glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    if (geometries && views) {
        geometries->Paint(windowWidth, windowHeight, views->GetDurationUpdateMks());
    }
}

void WindowManager::HandleKeyEvent(KeyCode key, KeyState state, int mods) {
    const int glfwKey = static_cast<int>(key);
    int glfwAction = GLFW_RELEASE;
    if (state == KeyState::Pressed) {
        glfwAction = GLFW_PRESS;
    } else if (state == KeyState::Repeated) {
        glfwAction = GLFW_REPEAT;
    }
    if (application_ && application_->RouteKey(glfwKey, glfwAction, mods)) {
        return;
    }

    if (!worldInstance) return;
    if (application_ && application_->GetState() != AppState::InGame) {
        return;
    }

    const bool keyDown = (state == KeyState::Pressed || state == KeyState::Repeated);

    // Update key states in camera
    if (auto camera = worldInstance->GetCurrentUserCamera()) {
        camera->UpdateKeyStatus(static_cast<int>(key), keyDown);
        if (static_cast<int>(key) == GLFW_KEY_RIGHT_SHIFT) {
            camera->UpdateKeyStatus(GLFW_KEY_LEFT_SHIFT, keyDown);
        }
    }

    // Special key processing
    if (state == KeyState::Pressed) {
        if (key == KeyCode::Key_Space) {
            if (auto camera = worldInstance->GetCurrentUserCamera()) {
                if (camera->TryToggleFlightOnDoubleSpace() && geometries) {
                    const std::string msg = camera->GetFreeMove()
                        ? "Flight ON (Space up, Shift down, 2xSpace off)"
                        : "Flight mode OFF";
                    geometries->ShowTransientMessage(msg, 2.5);
                }
            }
        }
        else if (key == KeyCode::Key_F12) {
            // reserved
        }
        else if (key == KeyCode::Key_Delete) {
            worldInstance->DelObjectByView();
        }
        else if (key == KeyCode::Key_F1) {
            SetSkyColor(0.5f, 0.7f, 1.0f, 1.0f); // Blue sky
        }
        else if (key == KeyCode::Key_F2) {
            SetSkyColor(1.0f, 0.6f, 0.3f, 1.0f); // Orange sky
        }
        else if (key == KeyCode::Key_F3) {
            SetSkyColor(0.1f, 0.1f, 0.3f, 1.0f); // Dark blue sky
        }
        else if (key == KeyCode::Key_F4) {
            SetSkyColor(0.6f, 0.6f, 0.6f, 1.0f); // Gray sky
        }
        else if (key == KeyCode::Key_F5) {
            SetGradientSky(!IsGradientSky());
        }
        else if (key == KeyCode::Key_F6) {
            SetSkyColor(1.0f, 0.6f, 0.3f, 1.0f);
            SetGradientSky(true);
        }
        else if (key == KeyCode::Key_F7) {
            if (auto anchor = worldInstance->FindPrefabAnchorFromView(
                    worldInstance->GetCurrentUserCamera()->GetPosition(),
                    worldInstance->GetCurrentUserCamera()->GetFront())) {
                worldInstance->PlacePrefab("tree_small", anchor.value());
            }
        }
        else if (key == KeyCode::Key_F8) {
            SetSkyColor(0.6f, 0.6f, 0.6f, 1.0f);
            SetGradientSky(true);
        }
        else if (key == KeyCode::Key_F9) {
            if (geometries) geometries->SetShowHud(!geometries->GetShowHud());
        }
        else if (key == KeyCode::Key_F10) {
            if (geometries) {
                geometries->SetShowPerformance(!geometries->GetShowPerformance());
            }
        }
        else if (key == KeyCode::Key_F11) {
            if (geometries) {
                geometries->SetShowCrosshair(!geometries->GetShowCrosshair());
            }
        }
    }
}

void WindowManager::ResetGameplayMouseCapture()
{
    isMousePressed = false;
    isLeftMouseButtonPressed = false;
    if (worldInstance && window) {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window, &x, &y);
        if (auto camera = worldInstance->GetCurrentUserCamera()) {
            camera->ResetMouseMove(x, y);
        }
    }
}

void WindowManager::HandleMouseButtonEvent(MouseButton button, bool pressed, glm::vec2 pos) {
    const glm::ivec2 fbPos = CursorToFramebufferPixels(window, pos.x, pos.y);
    const int glfwButton = button == MouseButton::Left   ? GLFW_MOUSE_BUTTON_LEFT
                         : button == MouseButton::Right  ? GLFW_MOUSE_BUTTON_RIGHT
                                                         : GLFW_MOUSE_BUTTON_MIDDLE;

    if (button == MouseButton::Right && !pressed) {
        isMousePressed = false;
    }

    if (application_ &&
        application_->RouteMouseButton(glfwButton, pressed, fbPos.x, fbPos.y)) {
        return;
    }

    if (!worldInstance) return;
    if (application_ && application_->GetState() != AppState::InGame) {
        return;
    }

    if (application_ && application_->WantsCaptureMouse()) {
        const bool allowPlace =
            button == MouseButton::Left && !pressed && application_->AllowsWorldMousePlacement();
        if (!allowPlace) {
            if (button == MouseButton::Right && !pressed) {
                isMousePressed = false;
            }
            return;
        }
    }

    if (button == MouseButton::Right) {
        if (pressed) {
            if (auto camera = worldInstance->GetCurrentUserCamera()) {
                camera->ResetMouseMove(pos.x, pos.y);
            }
            isMousePressed = true;
        }
    }
    else if (button == MouseButton::Left) {
        if (pressed) {
            leftMousePressed = std::chrono::steady_clock::now();
            isLeftMouseButtonPressed = true;
        } else {
            isLeftMouseButtonPressed = false;
            double delta_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - leftMousePressed).count() / 1000.0;
            
            const bool altDown = window &&
                (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                 glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);

            if (delta_time < 0.5) {
                const InventoryEntryRef* active = nullptr;
                if (Creature* controlled = worldInstance->GetControlledCreature()) {
                    active = controlled->GetInventory().GetActiveEntryRef();
                } else if (auto user = worldInstance->GetCurrentUser()) {
                    active = user->GetActiveHotbarEntry();
                }
                const bool placePrefab =
                    altDown
                    || (active && active->kind == InventoryEntryKind::Object && !active->id.empty());
                if (placePrefab) {
                    worldInstance->PlaceActivePrefabByView();
                } else {
                    worldInstance->AddObjectByView();
                }
            } else {
                worldInstance->DelObjectByView();
            }
        }
    }
}

void WindowManager::HandleMouseMoveEvent(glm::vec2 pos, glm::vec2 delta) {
    (void)delta;
    const glm::ivec2 fbPos = CursorToFramebufferPixels(window, pos.x, pos.y);
    if (application_ && application_->RouteMouseMove(fbPos.x, fbPos.y)) {
        return;
    }

    if (!worldInstance) {
        return;
    }
    if (application_ && application_->GetState() != AppState::InGame) {
        return;
    }
    if (application_ && application_->WantsCaptureMouse()) {
        return;
    }

    if (!isMousePressed) {
        return;
    }

    if (auto camera = worldInstance->GetCurrentUserCamera()) {
        camera->UpdateMouseMove(worldInstance, pos.x, pos.y);
    }
}

void WindowManager::HandleWindowResizeEvent(int width, int height) {
    windowWidth = width;
    windowHeight = height;
    glViewport(0, 0, width, height);
    
    const float aspect = static_cast<float>(width) / static_cast<float>(height ? height : 1);
    if (views) {
        if (auto camera = views->GetActiveCamera()) {
            camera->SetAspectRatio(aspect);
        }
    }
    if (worldInstance) {
        if (auto camera = worldInstance->GetCurrentUserCamera()) {
            camera->SetAspectRatio(aspect);
        }
    }
    
    if (textRenderer) {
        textRenderer->SetWindowSize(width, height);
    }
}

void WindowManager::Init(std::shared_ptr<Core> core_, 
                        std::shared_ptr<World> world_, 
                        std::shared_ptr<GeometryEngine> geometries_,
                        std::shared_ptr<ViewEngine> views_) {
    core = core_;
    worldInstance = world_;
    geometries = geometries_;
    views = views_;
}

void WindowManager::SetApplication(std::shared_ptr<Application> application)
{
    application_ = std::move(application);
}

void WindowManager::SetTextRenderer(std::shared_ptr<TextRenderer> text_renderer) {
    textRenderer = text_renderer;
    if (textRenderer) {
        textRenderer->SetWindowSize(windowWidth, windowHeight);
    }
}

void WindowManager::Shutdown() {
    if (inputManager) {
        inputManager->Shutdown();
    }
    
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    
    glfwTerminate();
    isInitialized = false;
    isRunning = false;
}

void WindowManager::RenderUI() {
    if (!textRenderer) {
        return;
    }
    
    // Save current OpenGL state
    GLboolean depthTestEnabled;
    glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
    GLboolean blendEnabled;
    glGetBooleanv(GL_BLEND, &blendEnabled);
    
    // Configure OpenGL for 2D rendering
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Display hints
    RenderHelpText();
    
    // Restore OpenGL state
    if (depthTestEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    
    if (blendEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
}

void WindowManager::RenderHelpText() {
    if (!textRenderer) return;
    
    float y = windowHeight - 30.0f; // Margin from top of screen
    float scale = 1.0f;
    glm::vec3 textColor(1.0f, 1.0f, 1.0f); // White color
    
    // Main control hints in English
    std::vector<std::string> helpLines = {
        "WASD - Movement, Space - Jump, double Space - Flight, Mouse - Look",
        "LMB - Place/remove, 0-9 primary hotbar, E inventory",
        "Shift+F10 - Procedural world (from config), Shift+F12 - Heightmap, Shift+F11 - Flat",
        "Delete - Remove block, F9 HUD, F10 perf, F11 crosshair"
    };
    
    for (const auto& line : helpLines) {
        textRenderer->RenderText(line, 10.0f, y, scale, textColor);
        y -= 25.0f; // Margin between lines
    }
}

void WindowManager::SetWindowSize(int width, int height) {
    if (window) {
        glfwSetWindowSize(window, width, height);
    }
}

void WindowManager::SetFullscreen(bool fullscreen) {
    if (window) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        
        if (fullscreen) {
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        } else {
            glfwSetWindowMonitor(window, nullptr, 100, 100, 1280, 720, 0);
        }
    }
}

bool WindowManager::ShouldClose() const {
    return glfwWindowShouldClose(window);
}

// Methods for sky color management
void WindowManager::SetSkyColor(float r, float g, float b, float a) {
    skyColor = glm::vec4(r, g, b, a);
    glClearColor(r, g, b, a);
    if (geometries) {
        geometries->SetSkyColor(r, g, b, a);
    }
}

void WindowManager::SetSkyColor(const glm::vec4& color) {
    skyColor = color;
    glClearColor(color.r, color.g, color.b, color.a);
    if (geometries) {
        geometries->SetSkyColor(color.r, color.g, color.b, color.a);
    }
}

glm::vec4 WindowManager::GetSkyColor() const {
    return skyColor;
}

void WindowManager::SetGradientSky(bool useGradient) {
    useGradientSky = useGradient;
    if (geometries) {
        geometries->SetGradientSky(useGradient);
    }
}

bool WindowManager::IsGradientSky() const {
    return useGradientSky;
}

// GLFW callback functions
void WindowManager::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    InputManager::GLFWFramebufferSizeCallback(window, width, height);
}

void WindowManager::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    InputManager::GLFWKeyCallback(window, key, scancode, action, mods);
}

void WindowManager::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    InputManager::GLFWMouseButtonCallback(window, button, action, mods);
}

void WindowManager::CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    InputManager::GLFWCursorPosCallback(window, xpos, ypos);
}

void WindowManager::ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    InputManager::GLFWScrollCallback(window, xoffset, yoffset);
}

void WindowManager::ErrorCallback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

void WindowManager::WindowCloseCallback(GLFWwindow* w) {
    auto* self = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
    if (self && self->core) {
        self->core->SaveSystem("config.json");
    }
}

} // namespace cutum
