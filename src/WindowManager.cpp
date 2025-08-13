#include "WindowManager.h"
#include "InputManager.h"
#include "Core.h"
#include "World.h"
#include "GeometryEngine.h"
#include "ViewEngine.h"
#include "User.h"
#include <iostream>
#include <stdexcept>

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
    // Инициализация GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // Настройка GLFW
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4); // MSAA

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // Создание окна
    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    windowWidth = width;
    windowHeight = height;

    // Создание контекста OpenGL
    glfwMakeContextCurrent(window);

    // Инициализация GLEW (должна быть после создания контекста)
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return false;
    }

    // Настройка OpenGL
    InitializeOpenGL();

    // Настройка callbacks
    SetupCallbacks();

    // Создание менеджера ввода
    inputManager = std::make_shared<InputManager>();
    inputManager->Initialize(window);

    isInitialized = true;
    return true;
}

void WindowManager::InitializeOpenGL() {
    // Включение тестирования глубины
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Включение MSAA
    glEnable(GL_MULTISAMPLE);

    // Включение смешивания для прозрачности
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Установка цвета очистки (небо)
    glClearColor(skyColor.r, skyColor.g, skyColor.b, skyColor.a);

    // Настройка viewport
    glViewport(0, 0, windowWidth, windowHeight);
}

void WindowManager::SetupCallbacks() {
    // Установка callback функций
    glfwSetFramebufferSizeCallback(window, InputManager::GLFWFramebufferSizeCallback);
    glfwSetKeyCallback(window, InputManager::GLFWKeyCallback);
    glfwSetMouseButtonCallback(window, InputManager::GLFWMouseButtonCallback);
    glfwSetCursorPosCallback(window, InputManager::GLFWCursorPosCallback);
    glfwSetScrollCallback(window, InputManager::GLFWScrollCallback);
    glfwSetErrorCallback(ErrorCallback);

    // Настройка callbacks для InputManager
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
        // Обновление времени
        auto currentTime = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<double>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;

        // Обработка ввода
        ProcessInput();

        // Обновление логики
        Update();

        // Рендеринг
        Render();

        // Обмен буферов и обработка событий
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}

void WindowManager::ProcessInput() {
    inputManager->Update();

    // Обработка клавиш для управления камерой
    if (worldInstance) {
        auto camera = worldInstance->GetCurrentUserCamera();
        if (camera) {
            // WASD движение
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
        }
    }
}

void WindowManager::Update() {
    if (worldInstance) {
        worldInstance->DoMovement();
    }
}

void WindowManager::Render() {
    // Очистка буферов
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (geometries && views) {
        views->UpdateFrameTime();
        geometries->Paint(windowWidth, windowHeight, views->GetDurationUpdateMks());
    }
}

void WindowManager::HandleKeyEvent(KeyCode key, KeyState state, int mods) {
    if (!worldInstance) return;

    bool pressed = (state == KeyState::Pressed);
    
    // Обновление состояния клавиш в камере
    worldInstance->GetCurrentUserCamera()->UpdateKeyStatus(static_cast<int>(key), pressed);

    // Обработка специальных клавиш
    if (pressed) {
        if (key >= KeyCode::Key_0 && key <= KeyCode::Key_9) {
            int index = static_cast<int>(key) - static_cast<int>(KeyCode::Key_0);
            worldInstance->GetCurrentUser()->SetActiveObjectTypeNameByIndex(static_cast<size_t>(index));
        }
        else if (key == KeyCode::Key_F12) {
            // Сброс мира (упрощенная версия без диалога)
            worldInstance->Create(worldInstance->GetWorldName());
        }
        else if (key == KeyCode::Key_Delete) {
            worldInstance->DelObjectByView();
        }
        else if (key == KeyCode::Key_F1) {
            SetSkyColor(0.5f, 0.7f, 1.0f, 1.0f); // Голубое небо
        }
        else if (key == KeyCode::Key_F2) {
            SetSkyColor(1.0f, 0.6f, 0.3f, 1.0f); // Оранжевое небо
        }
        else if (key == KeyCode::Key_F3) {
            SetSkyColor(0.1f, 0.1f, 0.3f, 1.0f); // Темно-синее небо
        }
        else if (key == KeyCode::Key_F4) {
            SetSkyColor(0.6f, 0.6f, 0.6f, 1.0f); // Серое небо
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

// Методы для управления цветом неба
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

// GLFW callback функции
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
