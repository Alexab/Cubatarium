#include "App/Platform/InputManager.h"
#include <iostream>

namespace cutum
{

// Static pointer to instance for GLFW callbacks
UInputManager *UInputManager::instance = nullptr;

UInputManager::UInputManager()
    : GlfwWindow(nullptr), MousePosition(0.0f, 0.0f),
      PreviousMousePosition(0.0f, 0.0f), MouseDelta(0.0f, 0.0f)
{
  instance = this;
  std::cout << "InputManager created, instance set to: " << instance
            << std::endl;
}

UInputManager::~UInputManager()
{
  std::cout << "InputManager destructor called, instance: " << instance
            << std::endl;
  Shutdown();
  if (instance == this)
  {
    instance = nullptr;
    std::cout << "InputManager instance set to nullptr" << std::endl;
  }
}

void UInputManager::Initialize(GLFWwindow *window)
{
  GlfwWindow = window;

  // Get initial mouse position
  double xpos, ypos;
  glfwGetCursorPos(window, &xpos, &ypos);
  MousePosition = glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));
  PreviousMousePosition = MousePosition;
}

void UInputManager::Shutdown()
{
  GlfwWindow = nullptr;
  KeyStates.clear();
  PreviousKeyStates.clear();
  KeyJustPressed.clear();
  KeyJustReleased.clear();
  MouseButtonStates.clear();
  PreviousMouseButtonStates.clear();
  MouseButtonJustPressed.clear();
  MouseButtonJustReleased.clear();
}

void UInputManager::Update()
{
  // Update key states
  for (auto &[key, Pressed] : KeyStates)
  {
    PreviousKeyStates[key] = Pressed;
    KeyJustPressed[key] = false;
    KeyJustReleased[key] = false;
  }

  // Update mouse Button states
  for (auto &[Button, Pressed] : MouseButtonStates)
  {
    PreviousMouseButtonStates[Button] = Pressed;
    MouseButtonJustPressed[Button] = false;
    MouseButtonJustReleased[Button] = false;
  }

  // Update mouse position
  PreviousMousePosition = MousePosition;
  MouseDelta = MousePosition - PreviousMousePosition;
}

bool UInputManager::IsKeyPressed(KeyCode key) const
{
  auto it = KeyStates.find(static_cast<int>(key));
  return it != KeyStates.end() && it->second;
}

bool UInputManager::IsKeyJustPressed(KeyCode key) const
{
  auto it = KeyJustPressed.find(static_cast<int>(key));
  return it != KeyJustPressed.end() && it->second;
}

bool UInputManager::IsKeyJustReleased(KeyCode key) const
{
  auto it = KeyJustReleased.find(static_cast<int>(key));
  return it != KeyJustReleased.end() && it->second;
}

bool UInputManager::IsMouseButtonPressed(MouseButton Button) const
{
  auto it = MouseButtonStates.find(static_cast<int>(Button));
  return it != MouseButtonStates.end() && it->second;
}

bool UInputManager::IsMouseButtonJustPressed(MouseButton Button) const
{
  auto it = MouseButtonJustPressed.find(static_cast<int>(Button));
  return it != MouseButtonJustPressed.end() && it->second;
}

bool UInputManager::IsMouseButtonJustReleased(MouseButton Button) const
{
  auto it = MouseButtonJustReleased.find(static_cast<int>(Button));
  return it != MouseButtonJustReleased.end() && it->second;
}

glm::vec2 UInputManager::GetMousePosition() const { return MousePosition; }

glm::vec2 UInputManager::GetMouseDelta() const { return MouseDelta; }

void UInputManager::SetKeyCallback(KeyCallbackFn callback)
{
  if (callback)
  {
    KeyCallback = callback;
  }
  else
  {
    std::cerr << "InputManager::SetKeyCallback: callback is null" << std::endl;
  }
}

void UInputManager::SetMouseButtonCallback(MouseButtonCallbackFn callback)
{
  MouseButtonCallback = callback;
}

void UInputManager::SetMouseMoveCallback(MouseMoveCallbackFn callback)
{
  MouseMoveCallback = callback;
}

void UInputManager::SetMouseScrollCallback(MouseScrollCallbackFn callback)
{
  MouseScrollCallback = callback;
}

void UInputManager::SetWindowResizeCallback(WindowResizeCallbackFn callback)
{
  WindowResizeCallback = callback;
}

void UInputManager::ResetAllKeyStatus()
{
  KeyStates.clear();
  PreviousKeyStates.clear();
  KeyJustPressed.clear();
  KeyJustReleased.clear();
  MouseButtonStates.clear();
  PreviousMouseButtonStates.clear();
  MouseButtonJustPressed.clear();
  MouseButtonJustReleased.clear();
}

// GLFW callback functions
void UInputManager::GLFWKeyCallback(GLFWwindow *window, int key, int scancode,
                                    int Action, int Mods)
{
  if (!instance)
  {
    std::cerr << "InputManager::GLFWKeyCallback: instance is null" << std::endl;
    return;
  }

  KeyState state = static_cast<KeyState>(Action);
  KeyCode key_code = static_cast<KeyCode>(key);

  // Update key state
  if (Action == GLFW_PRESS)
  {
    instance->KeyStates[key] = true;
    instance->KeyJustPressed[key] = true;
  }
  else if (Action == GLFW_RELEASE)
  {
    instance->KeyStates[key] = false;
    instance->KeyJustReleased[key] = true;
  }

  // Call user callback
  if (instance->KeyCallback)
  {
    try
    {
      instance->KeyCallback(key_code, state, Mods);
    }
    catch (const std::exception &e)
    {
      std::cerr << "Exception in KeyCallback: " << e.what() << std::endl;
    }
    catch (...)
    {
      std::cerr << "Unknown exception in KeyCallback" << std::endl;
    }
  }
}

void UInputManager::GLFWMouseButtonCallback(GLFWwindow *window, int Button,
                                            int Action, int Mods)
{
  if (!instance)
    return;

  MouseButton mouseButton = static_cast<MouseButton>(Button);
  bool Pressed = (Action == GLFW_PRESS);

  // Update mouse Button state
  if (Pressed)
  {
    instance->MouseButtonStates[Button] = true;
    instance->MouseButtonJustPressed[Button] = true;
  }
  else
  {
    instance->MouseButtonStates[Button] = false;
    instance->MouseButtonJustReleased[Button] = true;
  }

  double x = 0.0;
  double y = 0.0;
  glfwGetCursorPos(window, &x, &y);
  const glm::vec2 pos(static_cast<float>(x), static_cast<float>(y));
  instance->MousePosition = pos;

  if (instance->MouseButtonCallback)
  {
    instance->MouseButtonCallback(mouseButton, Pressed, pos);
  }
}

void UInputManager::GLFWCursorPosCallback(GLFWwindow *window, double xpos,
                                          double ypos)
{
  if (!instance)
    return;

  glm::vec2 newPosition(static_cast<float>(xpos), static_cast<float>(ypos));
  glm::vec2 delta = newPosition - instance->MousePosition;

  instance->MousePosition = newPosition;

  // Call user callback
  if (instance->MouseMoveCallback)
  {
    instance->MouseMoveCallback(newPosition, delta);
  }
}

void UInputManager::GLFWScrollCallback(GLFWwindow *window, double Xoffset,
                                       double Yoffset)
{
  if (!instance || !instance->MouseScrollCallback)
    return;

  instance->MouseScrollCallback(Xoffset, Yoffset);
}

void UInputManager::GLFWFramebufferSizeCallback(GLFWwindow *window, int width,
                                                int height)
{
  if (!instance || !instance->WindowResizeCallback)
    return;

  instance->WindowResizeCallback(width, height);
}

} // namespace cutum
