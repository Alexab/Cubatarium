#include "InputManager.h"
#include <iostream>

namespace cutum {

// Статический указатель на экземпляр для GLFW callbacks
InputManager* InputManager::instance = nullptr;

InputManager::InputManager()
    : glfwWindow(nullptr)
    , mousePosition(0.0f, 0.0f)
    , previousMousePosition(0.0f, 0.0f)
    , mouseDelta(0.0f, 0.0f)
{
    instance = this;
    std::cout << "InputManager created, instance set to: " << instance << std::endl;
}

InputManager::~InputManager() {
    std::cout << "InputManager destructor called, instance: " << instance << std::endl;
    Shutdown();
    if (instance == this) {
        instance = nullptr;
        std::cout << "InputManager instance set to nullptr" << std::endl;
    }
}

void InputManager::Initialize(GLFWwindow* window) {
    glfwWindow = window;
    
    // Получение начальной позиции мыши
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    mousePosition = glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));
    previousMousePosition = mousePosition;
}

void InputManager::Shutdown() {
    glfwWindow = nullptr;
    keyStates.clear();
    previousKeyStates.clear();
    keyJustPressed.clear();
    keyJustReleased.clear();
    mouseButtonStates.clear();
    previousMouseButtonStates.clear();
    mouseButtonJustPressed.clear();
    mouseButtonJustReleased.clear();
}

void InputManager::Update() {
    // Обновление состояния клавиш
    for (auto& [key, pressed] : keyStates) {
        previousKeyStates[key] = pressed;
        keyJustPressed[key] = false;
        keyJustReleased[key] = false;
    }

    // Обновление состояния кнопок мыши
    for (auto& [button, pressed] : mouseButtonStates) {
        previousMouseButtonStates[button] = pressed;
        mouseButtonJustPressed[button] = false;
        mouseButtonJustReleased[button] = false;
    }

    // Обновление позиции мыши
    previousMousePosition = mousePosition;
    mouseDelta = mousePosition - previousMousePosition;
}

bool InputManager::IsKeyPressed(KeyCode key) const {
    auto it = keyStates.find(static_cast<int>(key));
    return it != keyStates.end() && it->second;
}

bool InputManager::IsKeyJustPressed(KeyCode key) const {
    auto it = keyJustPressed.find(static_cast<int>(key));
    return it != keyJustPressed.end() && it->second;
}

bool InputManager::IsKeyJustReleased(KeyCode key) const {
    auto it = keyJustReleased.find(static_cast<int>(key));
    return it != keyJustReleased.end() && it->second;
}

bool InputManager::IsMouseButtonPressed(MouseButton button) const {
    auto it = mouseButtonStates.find(static_cast<int>(button));
    return it != mouseButtonStates.end() && it->second;
}

bool InputManager::IsMouseButtonJustPressed(MouseButton button) const {
    auto it = mouseButtonJustPressed.find(static_cast<int>(button));
    return it != mouseButtonJustPressed.end() && it->second;
}

bool InputManager::IsMouseButtonJustReleased(MouseButton button) const {
    auto it = mouseButtonJustReleased.find(static_cast<int>(button));
    return it != mouseButtonJustReleased.end() && it->second;
}

glm::vec2 InputManager::GetMousePosition() const {
    return mousePosition;
}

glm::vec2 InputManager::GetMouseDelta() const {
    return mouseDelta;
}

void InputManager::SetKeyCallback(KeyCallback callback) {
    if (callback) {
        keyCallback = callback;
    } else {
        std::cerr << "InputManager::SetKeyCallback: callback is null" << std::endl;
    }
}

void InputManager::SetMouseButtonCallback(MouseButtonCallback callback) {
    mouseButtonCallback = callback;
}

void InputManager::SetMouseMoveCallback(MouseMoveCallback callback) {
    mouseMoveCallback = callback;
}

void InputManager::SetMouseScrollCallback(MouseScrollCallback callback) {
    mouseScrollCallback = callback;
}

void InputManager::SetWindowResizeCallback(WindowResizeCallback callback) {
    windowResizeCallback = callback;
}

void InputManager::ResetAllKeyStatus() {
    keyStates.clear();
    previousKeyStates.clear();
    keyJustPressed.clear();
    keyJustReleased.clear();
    mouseButtonStates.clear();
    previousMouseButtonStates.clear();
    mouseButtonJustPressed.clear();
    mouseButtonJustReleased.clear();
}

// GLFW callback функции
void InputManager::GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (!instance) {
        std::cerr << "InputManager::GLFWKeyCallback: instance is null" << std::endl;
        return;
    }

    KeyState state = static_cast<KeyState>(action);
    KeyCode keyCode = static_cast<KeyCode>(key);

    // Обновление состояния клавиши
    if (action == GLFW_PRESS) {
        instance->keyStates[key] = true;
        instance->keyJustPressed[key] = true;
    } else if (action == GLFW_RELEASE) {
        instance->keyStates[key] = false;
        instance->keyJustReleased[key] = true;
    }

    // Вызов пользовательского callback
    if (instance->keyCallback) {
        try {
            instance->keyCallback(keyCode, state, mods);
        } catch (const std::exception& e) {
            std::cerr << "Exception in keyCallback: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception in keyCallback" << std::endl;
        }
    }
}

void InputManager::GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (!instance) return;

    MouseButton mouseButton = static_cast<MouseButton>(button);
    bool pressed = (action == GLFW_PRESS);

    // Обновление состояния кнопки мыши
    if (pressed) {
        instance->mouseButtonStates[button] = true;
        instance->mouseButtonJustPressed[button] = true;
    } else {
        instance->mouseButtonStates[button] = false;
        instance->mouseButtonJustReleased[button] = true;
    }

    // Вызов пользовательского callback
    if (instance->mouseButtonCallback) {
        instance->mouseButtonCallback(mouseButton, pressed, instance->mousePosition);
    }
}

void InputManager::GLFWCursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (!instance) return;

    glm::vec2 newPosition(static_cast<float>(xpos), static_cast<float>(ypos));
    glm::vec2 delta = newPosition - instance->mousePosition;
    
    instance->mousePosition = newPosition;

    // Вызов пользовательского callback
    if (instance->mouseMoveCallback) {
        instance->mouseMoveCallback(newPosition, delta);
    }
}

void InputManager::GLFWScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (!instance || !instance->mouseScrollCallback) return;

    instance->mouseScrollCallback(xoffset, yoffset);
}

void InputManager::GLFWFramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    if (!instance || !instance->windowResizeCallback) return;

    instance->windowResizeCallback(width, height);
}

} // namespace cutum
