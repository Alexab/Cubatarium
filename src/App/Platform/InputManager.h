#ifndef INPUTMANAGER_H
#define INPUTMANAGER_H

#if defined(__ANDROID__)
#include "App/Platform/GlfwKeyCompat.h"
struct GLFWwindow;
#else
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif
#include <functional>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace cutum
{

// Key codes (similar to Qt::Key)
enum class KeyCode
{
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

// Mouse Button codes
enum class MouseButton
{
  Left = GLFW_MOUSE_BUTTON_LEFT,
  Right = GLFW_MOUSE_BUTTON_RIGHT,
  Middle = GLFW_MOUSE_BUTTON_MIDDLE
};

// Key state
enum class KeyState
{
  Released = GLFW_RELEASE,
  Pressed = GLFW_PRESS,
  Repeated = GLFW_REPEAT
};

// Event Types
enum class EventType
{
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
struct InputEvent
{
  EventType type;
  KeyCode key;
  MouseButton mouseButton;
  glm::vec2 MousePosition;
  glm::vec2 MouseDelta;
  double scrollDelta;
  int WindowWidth;
  int WindowHeight;
  bool isPressed;
  int modifiers;
};

// Callback functions
using KeyCallbackFn = std::function<void(KeyCode, KeyState, int)>;
using MouseButtonCallbackFn = std::function<void(MouseButton, bool, glm::vec2)>;
using MouseMoveCallbackFn = std::function<void(glm::vec2, glm::vec2)>;
using MouseScrollCallbackFn = std::function<void(double, double)>;
using WindowResizeCallbackFn = std::function<void(int, int)>;

class UInputManager
{
public:
  UInputManager();
  ~UInputManager();

  // Инициализация
  void Initialize(GLFWwindow *window);
  void Shutdown();

  // Обновление состояния
  void Update();

  // Проверка состояния клавиш
  bool IsKeyPressed(KeyCode key) const;
  bool IsKeyJustPressed(KeyCode key) const;
  bool IsKeyJustReleased(KeyCode key) const;

  // Проверка состояния мыши
  bool IsMouseButtonPressed(MouseButton Button) const;
  bool IsMouseButtonJustPressed(MouseButton Button) const;
  bool IsMouseButtonJustReleased(MouseButton Button) const;

  // Получение позиции мыши
  glm::vec2 GetMousePosition() const;
  glm::vec2 GetMouseDelta() const;

  // Callback регистрация
  void SetKeyCallback(KeyCallbackFn callback);
  void SetMouseButtonCallback(MouseButtonCallbackFn callback);
  void SetMouseMoveCallback(MouseMoveCallbackFn callback);
  void SetMouseScrollCallback(MouseScrollCallbackFn callback);
  void SetWindowResizeCallback(WindowResizeCallbackFn callback);

  // GLFW callback функции (статичные)
  static void GLFWKeyCallback(GLFWwindow *window, int key, int scancode,
                              int Action, int Mods);
  static void GLFWMouseButtonCallback(GLFWwindow *window, int Button,
                                      int Action, int Mods);
  static void GLFWCursorPosCallback(GLFWwindow *window, double xpos,
                                    double ypos);
  static void GLFWScrollCallback(GLFWwindow *window, double Xoffset,
                                 double Yoffset);
  static void GLFWFramebufferSizeCallback(GLFWwindow *window, int width,
                                          int height);

  // Сброс состояния
  void ResetAllKeyStatus();

private:
  // GLFW window
  GLFWwindow *GlfwWindow;

  // Состояние клавиш
  std::unordered_map<int, bool> KeyStates;
  std::unordered_map<int, bool> PreviousKeyStates;
  std::unordered_map<int, bool> KeyJustPressed;
  std::unordered_map<int, bool> KeyJustReleased;

  // Состояние мыши
  std::unordered_map<int, bool> MouseButtonStates;
  std::unordered_map<int, bool> PreviousMouseButtonStates;
  std::unordered_map<int, bool> MouseButtonJustPressed;
  std::unordered_map<int, bool> MouseButtonJustReleased;

  // Позиция мыши
  glm::vec2 MousePosition;
  glm::vec2 PreviousMousePosition;
  glm::vec2 MouseDelta;

  // Callback функции
  KeyCallbackFn KeyCallback;
  MouseButtonCallbackFn MouseButtonCallback;
  MouseMoveCallbackFn MouseMoveCallback;
  MouseScrollCallbackFn MouseScrollCallback;
  WindowResizeCallbackFn WindowResizeCallback;

  // Статический указатель на экземпляр для GLFW callbacks
  static UInputManager *instance;
};

} // namespace cutum

#endif // INPUTMANAGER_H
