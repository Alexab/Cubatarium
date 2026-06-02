#ifndef WINDOWMANAGER_H
#define WINDOWMANAGER_H

#define GLFW_INCLUDE_NONE
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <functional>
#include <chrono>
#include "TextRenderer.h"

namespace cutum {

class Core;
class World;
class GeometryEngine;
class ViewEngine;
class InputManager;
class Application;

enum class KeyCode;
enum class KeyState;
enum class MouseButton;

class WindowManager {
public:
    WindowManager();
    ~WindowManager();

    bool Initialize(int width = 1280, int height = 720, const char* title = "Cubatarium");
    void Run();
    void Shutdown();

    void Init(std::shared_ptr<Core> core_, 
              std::shared_ptr<World> world_, 
              std::shared_ptr<GeometryEngine> geometries_,
              std::shared_ptr<ViewEngine> views_);

    void SetApplication(std::shared_ptr<Application> application);
    
    void SetTextRenderer(std::shared_ptr<TextRenderer> text_renderer);

    void SetWindowSize(int width, int height);
    void SetFullscreen(bool fullscreen);
    bool ShouldClose() const;
    
    int GetWidth() const { return windowWidth; }
    int GetHeight() const { return windowHeight; }
    
    GLFWwindow* GetWindow() const { return window; }

    /// Сброс ПКМ-обзора (например при Esc → меню).
    void ResetGameplayMouseCapture();

    void SetSkyColor(float r, float g, float b, float a = 1.0f);
    void SetSkyColor(const glm::vec4& color);
    glm::vec4 GetSkyColor() const;
    void SetGradientSky(bool useGradient);
    bool IsGradientSky() const;

private:
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void ErrorCallback(int error, const char* description);
    static void WindowCloseCallback(GLFWwindow* window);

    void ProcessInput();
    void Render();
    void Update();
    void SetupCallbacks();
    void InitializeOpenGL();
    
    void HandleKeyEvent(KeyCode key, KeyState state, int mods);
    void HandleMouseButtonEvent(MouseButton button, bool pressed, glm::vec2 pos);
    void HandleMouseMoveEvent(glm::vec2 pos, glm::vec2 delta);
    void HandleWindowResizeEvent(int width, int height);
    
    void RenderUI();
    void RenderHelpText();

    GLFWwindow* window;
    
    int windowWidth;
    int windowHeight;
    
    bool isRunning;
    bool isInitialized;
    
    std::shared_ptr<Core> core;
    std::shared_ptr<GeometryEngine> geometries;
    std::shared_ptr<ViewEngine> views;
    std::shared_ptr<World> worldInstance;
    std::shared_ptr<InputManager> inputManager;
    std::shared_ptr<TextRenderer> textRenderer;
    std::shared_ptr<Application> application_;
    
    std::chrono::high_resolution_clock::time_point lastFrameTime;
    std::chrono::steady_clock::time_point lastAutosaveTime_;
    static constexpr double kAutosaveIntervalSec = 60.0;
    double deltaTime;
    
    glm::vec2 mousePressPosition;
    bool isMousePressed;
    bool isLeftMouseButtonPressed;
    std::chrono::steady_clock::time_point leftMousePressed;
    
    glm::vec4 skyColor;
    bool useGradientSky;
};

}

#endif // WINDOWMANAGER_H
