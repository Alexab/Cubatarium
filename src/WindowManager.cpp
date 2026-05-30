#include "WindowManager.h"
#include "InputManager.h"
#include "Core.h"
#include "World.h"
#include "GeometryEngine.h"
#include "ViewEngine.h"
#include "User.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace cutum {

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

        // Input processing
        ProcessInput();

        // Logic update
        Update();

        // Rendering
        Render();

        // Buffer swap and event processing
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}

void WindowManager::ProcessInput() {
    inputManager->Update();

    // Camera control key processing
    if (worldInstance) {
        auto camera = worldInstance->GetCurrentUserCamera();
        if (camera) {
            // WASD movement
            if (inputManager->IsKeyPressed(KeyCode::Key_W)) {
                camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_W), true);
            }
            if (inputManager->IsKeyPressed(KeyCode::Key_S)) {
                camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_S), true);
            }
            if (inputManager->IsKeyPressed(KeyCode::Key_A)) {
                camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_A), true);
            }
            if (inputManager->IsKeyPressed(KeyCode::Key_D)) {
                camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_D), true);
            }
            if (inputManager->IsKeyPressed(KeyCode::Key_Space)) {
                camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_Space), true);
            }
            if (inputManager->IsKeyPressed(KeyCode::Key_Shift)) {
                camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_Shift), true);
            }
            // Q and E for up-down movement
            if (inputManager->IsKeyPressed(KeyCode::Key_Q)) {
                camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_Q), true);
            }
            if (inputManager->IsKeyPressed(KeyCode::Key_E)) {
                camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_E), true);
            }
        }
    }
}

void WindowManager::Update() {
    if (worldInstance) {
        worldInstance->DoMovement();
    }
}

void WindowManager::Render() {
    // Buffer clearing
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (geometries && views) {
        views->UpdateFrameTime();
        geometries->Paint(windowWidth, windowHeight, views->GetDurationUpdateMks());
    }
    
    // Disable UI display over 3D scene
    // RenderUI();
}

void WindowManager::HandleKeyEvent(KeyCode key, KeyState state, int mods) {
    if (!worldInstance) return;

    bool pressed = (state == KeyState::Pressed);
    
    // Update key states in camera
    worldInstance->GetCurrentUserCamera()->UpdateKeyStatus(static_cast<int>(key), pressed);

    // Special key processing
    if (pressed) {
        if (key >= KeyCode::Key_0 && key <= KeyCode::Key_9) {
            int index = static_cast<int>(key) - static_cast<int>(KeyCode::Key_0);
            worldInstance->GetCurrentUser()->SetActiveObjectTypeNameByIndex(static_cast<size_t>(index));
        }
        else if (key == KeyCode::Key_F12) {
            if (core) {
                core->CreateWorld(worldInstance->GetWorldName());
            } else {
                worldInstance->Create(worldInstance->GetWorldName());
            }
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
            SetSkyColor(0.1f, 0.1f, 0.3f, 1.0f);
            SetGradientSky(true);
        }
        else if (key == KeyCode::Key_F8) {
            SetSkyColor(0.6f, 0.6f, 0.6f, 1.0f);
            SetGradientSky(true);
        }
        else if (key == KeyCode::Key_F9) {
            if (geometries) geometries->SetShowHud(!geometries->GetShowHud());
        }
        else if (key == KeyCode::Key_F10) {
            if (geometries) geometries->SetShowPerformance(!geometries->GetShowPerformance());
        }
        else if (key == KeyCode::Key_F11) {
            if (geometries) geometries->SetShowCrosshair(!geometries->GetShowCrosshair());
        }
    }
}

void WindowManager::HandleMouseButtonEvent(MouseButton button, bool pressed, glm::vec2 pos) {
    if (!worldInstance) return;

    if (button == MouseButton::Right) {
        if (pressed) {
            worldInstance->GetCurrentUserCamera()->ResetMouseMove(pos.x, pos.y);
            isMousePressed = true;
        } else {
            isMousePressed = false;
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
            
            if (delta_time < 0.5) {
                worldInstance->AddObjectByView();
            } else {
                worldInstance->DelObjectByView();
            }
        }
    }
}

void WindowManager::HandleMouseMoveEvent(glm::vec2 pos, glm::vec2 delta) {
    if (!worldInstance || !isMousePressed) return;
    
    worldInstance->GetCurrentUserCamera()->UpdateMouseMove(worldInstance, pos.x, pos.y);
}

void WindowManager::HandleWindowResizeEvent(int width, int height) {
    windowWidth = width;
    windowHeight = height;
    glViewport(0, 0, width, height);
    
    if (views) {
        float aspect = static_cast<float>(width) / static_cast<float>(height ? height : 1);
        views->GetActiveCamera()->SetAspectRatio(aspect);
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
        "WASD - Movement, Space/Shift - Up/Down, Mouse - Camera rotation",
        "LMB (short) - Place block, LMB (hold) - Remove block, 0-9 - Block type",
        "F12 - New flat world (save), Delete - Remove targeted block",
        "F1-F4 - Sky color, F5 - Toggle gradient sky, F9 - Toggle HUD"
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

} // namespace cutum
