#ifndef WINDOWMANAGER_H
#define WINDOWMANAGER_H

#define GLFW_INCLUDE_NONE
#include "Render/Engine/TextRenderer.h"
#include "Render/GlIncludes.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>

namespace cutum
{

class UCore;
class UWorld;
class UGeometryEngine;
class UViewEngine;
class UInputManager;
class UApplication;
class UBlockInputController;

enum class KeyCode;
enum class KeyState;
enum class MouseButton;

class UWindowManager
{
public:
  UWindowManager();
  ~UWindowManager();

  bool Initialize(int width = 1280, int height = 720,
                  const char *title = "Cubatarium");
  void Run();
  void Shutdown();

  /// Initializes the window manager with the specified parameters.
  /// \param Core The core instance.
  /// \param World The world instance.
  /// \param Geometries The geometry engine instance.
  /// \param Views The view engine instance.
  void SetInstances(std::shared_ptr<UCore> core, std::shared_ptr<UWorld> world,
                    std::shared_ptr<UGeometryEngine> geometries,
                    std::shared_ptr<UViewEngine> views);

  void SetApplication(std::shared_ptr<UApplication> application);

  void SetTextRenderer(std::shared_ptr<UTextRenderer> text_renderer);

  void SetWindowSize(int width, int height);
  void SetFullscreen(bool fullscreen);
  bool ShouldClose() const;

  int GetWidth() const { return WindowWidth; }
  int GetHeight() const { return WindowHeight; }

  GLFWwindow *GetWindow() const { return Window; }

  /// Сброс ПКМ-обзора (например при Esc → меню).
  void ResetGameplayMouseCapture();

  void SetSkyColor(float r, float g, float b, float a = 1.0f);
  void SetSkyColor(const glm::vec4 &color);
  glm::vec4 GetSkyColor() const;
  void SetGradientSky(bool useGradient);
  bool IsGradientSky() const;

private:
  static void FramebufferSizeCallback(GLFWwindow *window, int width,
                                      int height);
  static void KeyCallback(GLFWwindow *window, int key, int scancode, int Action,
                          int Mods);
  static void MouseButtonCallback(GLFWwindow *window, int Button, int Action,
                                  int Mods);
  static void CursorPosCallback(GLFWwindow *window, double xpos, double ypos);
  static void ScrollCallback(GLFWwindow *window, double Xoffset,
                             double Yoffset);
  static void ErrorCallback(int error, const char *description);
  static void WindowCloseCallback(GLFWwindow *window);

  void ProcessInput();
  void Render();
  void Update();
  void SetupCallbacks();
  void InitializeOpenGL();

  void HandleKeyEvent(KeyCode key, KeyState state, int Mods);
  void HandleMouseButtonEvent(MouseButton Button, bool Pressed, glm::vec2 pos);
  void HandleMouseMoveEvent(glm::vec2 pos, glm::vec2 delta);
  void HandleWindowResizeEvent(int width, int height);

  void RenderUI();
  void RenderHelpText();

private: // Instances of core systems
  std::shared_ptr<UCore> Core;
  std::shared_ptr<UGeometryEngine> Geometries;
  std::shared_ptr<UViewEngine> Views;
  std::shared_ptr<UWorld> World;
  std::shared_ptr<UInputManager> InputManager;
  std::shared_ptr<UTextRenderer> TextRenderer;
  std::shared_ptr<UApplication> Application;

  std::unique_ptr<UBlockInputController> BlockInput;

private: // Window and rendering state
  GLFWwindow *Window;

  int WindowWidth;
  int WindowHeight;

  bool IsRunning;
  bool IsInitialized;

  std::chrono::high_resolution_clock::time_point LastFrameTime;
  std::chrono::steady_clock::time_point LastAutosaveTime;
  static constexpr double KAutosaveIntervalSec = 60.0;
  double DeltaTime;

  glm::vec4 SkyColor;
  bool UseGradientSky;
};

} // namespace cutum

#endif // WINDOWMANAGER_H
