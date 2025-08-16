#ifndef INPUTMANAGER_H
#define INPUTMANAGER_H

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <functional>
#include <vector>

namespace cutum {

// Key codes (similar to Qt::Key)
enum class KeyCode {
    Key_0 = GLFW_KEY_0,
    Key_1 = GLFW_KEY_1,
    Key_2 = GLFW_KEY_2,
    Key_3 = GLFW_KEY_3,
    Key_4 = GLFW_KEY_4,
    Key_5 = GLFW_KEY_5,
    Key_6 = GLFW_KEY_6,
    Key_7 = GLFW_KEY_7,
    Key_8 = GLFW_KEY_8,
    Key_9 = GLFW_KEY_9,
    Key_W = GLFW_KEY_W,
    Key_A = GLFW_KEY_A,
    Key_S = GLFW_KEY_S,
    Key_D = GLFW_KEY_D,
    Key_Q = GLFW_KEY_Q,
    Key_E = GLFW_KEY_E,
    Key_Space = GLFW_KEY_SPACE,
    Key_Shift = GLFW_KEY_LEFT_SHIFT,
    Key_Ctrl = GLFW_KEY_LEFT_CONTROL,
    Key_Alt = GLFW_KEY_LEFT_ALT,
    Key_Escape = GLFW_KEY_ESCAPE,
    Key_Enter = GLFW_KEY_ENTER,
    Key_Backspace = GLFW_KEY_BACKSPACE,
    Key_Tab = GLFW_KEY_TAB,
    Key_F1 = GLFW_KEY_F1,
    Key_F2 = GLFW_KEY_F2,
    Key_F3 = GLFW_KEY_F3,
    Key_F4 = GLFW_KEY_F4,
    Key_F5 = GLFW_KEY_F5,
    Key_F6 = GLFW_KEY_F6,
    Key_F7 = GLFW_KEY_F7,
    Key_F8 = GLFW_KEY_F8,
    Key_F9 = GLFW_KEY_F9,
    Key_F10 = GLFW_KEY_F10,
    Key_F11 = GLFW_KEY_F11,
    Key_F12 = GLFW_KEY_F12,
    Key_Delete = GLFW_KEY_DELETE,
    Key_Insert = GLFW_KEY_INSERT,
    Key_Home = GLFW_KEY_HOME,
    Key_End = GLFW_KEY_END,
    Key_PageUp = GLFW_KEY_PAGE_UP,
    Key_PageDown = GLFW_KEY_PAGE_DOWN,
    Key_Up = GLFW_KEY_UP,
    Key_Down = GLFW_KEY_DOWN,
    Key_Left = GLFW_KEY_LEFT,
    Key_Right = GLFW_KEY_RIGHT
};

// Mouse button codes
enum class MouseButton {
    Left = GLFW_MOUSE_BUTTON_LEFT,
    Right = GLFW_MOUSE_BUTTON_RIGHT,
    Middle = GLFW_MOUSE_BUTTON_MIDDLE
};

// Key state
enum class KeyState {
    Released = GLFW_RELEASE,
    Pressed = GLFW_PRESS,
    Repeated = GLFW_REPEAT
};

// Event types
enum class EventType {
    KeyPress,
    KeyRelease,
    KeyRepeat,
    MouseButtonPress,
    MouseButtonRelease,
    MouseMove,
    MouseScroll,
    WindowResize,
    WindowFocus,
    WindowClose
};

// Event structure
struct InputEvent {
    EventType type;
    KeyCode key;
    MouseButton mouseButton;
    glm::vec2 mousePosition;
    glm::vec2 mouseDelta;
    double scrollDelta;
    int windowWidth;
    int windowHeight;
    bool isPressed;
    int modifiers;
};

// Callback functions
using KeyCallback = std::function<void(KeyCode, KeyState, int)>;
using MouseButtonCallback = std::function<void(MouseButton, bool, glm::vec2)>;
using MouseMoveCallback = std::function<void(glm::vec2, glm::vec2)>;
using MouseScrollCallback = std::function<void(double, double)>;
using WindowResizeCallback = std::function<void(int, int)>;

class InputManager {
public:
    InputManager();
    ~InputManager();

    // Инициализация
    void Initialize(GLFWwindow* window);
    void Shutdown();

    // Обновление состояния
    void Update();

    // Проверка состояния клавиш
    bool IsKeyPressed(KeyCode key) const;
    bool IsKeyJustPressed(KeyCode key) const;
    bool IsKeyJustReleased(KeyCode key) const;

    // Проверка состояния мыши
    bool IsMouseButtonPressed(MouseButton button) const;
    bool IsMouseButtonJustPressed(MouseButton button) const;
    bool IsMouseButtonJustReleased(MouseButton button) const;

    // Получение позиции мыши
    glm::vec2 GetMousePosition() const;
    glm::vec2 GetMouseDelta() const;

    // Callback регистрация
    void SetKeyCallback(KeyCallback callback);
    void SetMouseButtonCallback(MouseButtonCallback callback);
    void SetMouseMoveCallback(MouseMoveCallback callback);
    void SetMouseScrollCallback(MouseScrollCallback callback);
    void SetWindowResizeCallback(WindowResizeCallback callback);

    // GLFW callback функции (статичные)
    static void GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void GLFWCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void GLFWScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void GLFWFramebufferSizeCallback(GLFWwindow* window, int width, int height);

    // Сброс состояния
    void ResetAllKeyStatus();

private:
    // GLFW window
    GLFWwindow* glfwWindow;

    // Состояние клавиш
    std::unordered_map<int, bool> keyStates;
    std::unordered_map<int, bool> previousKeyStates;
    std::unordered_map<int, bool> keyJustPressed;
    std::unordered_map<int, bool> keyJustReleased;

    // Состояние мыши
    std::unordered_map<int, bool> mouseButtonStates;
    std::unordered_map<int, bool> previousMouseButtonStates;
    std::unordered_map<int, bool> mouseButtonJustPressed;
    std::unordered_map<int, bool> mouseButtonJustReleased;

    // Позиция мыши
    glm::vec2 mousePosition;
    glm::vec2 previousMousePosition;
    glm::vec2 mouseDelta;

    // Callback функции
    KeyCallback keyCallback;
    MouseButtonCallback mouseButtonCallback;
    MouseMoveCallback mouseMoveCallback;
    MouseScrollCallback mouseScrollCallback;
    WindowResizeCallback windowResizeCallback;

    // Статический указатель на экземпляр для GLFW callbacks
    static InputManager* instance;
};

} // namespace cutum

#endif // INPUTMANAGER_H
