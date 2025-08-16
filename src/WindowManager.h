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

// Forward declarations for input types
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

    // Initialize components
    void Init(std::shared_ptr<Core> core_, 
              std::shared_ptr<World> world_, 
              std::shared_ptr<GeometryEngine> geometries_,
              std::shared_ptr<ViewEngine> views_);
    
    // Set TextRenderer
    void SetTextRenderer(std::shared_ptr<TextRenderer> text_renderer);

    // Window management
    void SetWindowSize(int width, int height);
    void SetFullscreen(bool fullscreen);
    bool ShouldClose() const;
    
    // Get window dimensions
    int GetWidth() const { return windowWidth; }
    int GetHeight() const { return windowHeight; }
    
    // Get GLFW window for passing to other systems
    GLFWwindow* GetWindow() const { return window; }

    // Methods for sky color management (moved from MainWidget)
    void SetSkyColor(float r, float g, float b, float a = 1.0f);
    void SetSkyColor(const glm::vec4& color);
    glm::vec4 GetSkyColor() const;
    void SetGradientSky(bool useGradient);
    bool IsGradientSky() const;

private:
    // GLFW callbacks
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void ErrorCallback(int error, const char* description);

    // Internal methods
    void ProcessInput();
    void Render();
    void Update();
    void SetupCallbacks();
    void InitializeOpenGL();
    
    // Event handlers
    void HandleKeyEvent(KeyCode key, KeyState state, int mods);
    void HandleMouseButtonEvent(MouseButton button, bool pressed, glm::vec2 pos);
    void HandleMouseMoveEvent(glm::vec2 pos, glm::vec2 delta);
    void HandleWindowResizeEvent(int width, int height);
    
    // Methods for UI rendering
    void RenderUI();
    void RenderHelpText();

    // GLFW window
    GLFWwindow* window;
    
    // Window dimensions
    int windowWidth;
    int windowHeight;
    
    // Application state
    bool isRunning;
    bool isInitialized;
    
    // System components
    std::shared_ptr<Core> core;
    std::shared_ptr<GeometryEngine> geometries;
    std::shared_ptr<ViewEngine> views;
    std::shared_ptr<World> worldInstance;
    std::shared_ptr<InputManager> inputManager;
    std::shared_ptr<TextRenderer> textRenderer;
    
    // Time
    std::chrono::high_resolution_clock::time_point lastFrameTime;
    double deltaTime;
    
    // Mouse state
    glm::vec2 mousePressPosition;
    bool isMousePressed;
    bool isLeftMouseButtonPressed;
    std::chrono::steady_clock::time_point leftMousePressed;
    
    // Sky color
    glm::vec4 skyColor;
    bool useGradientSky;
};

} // namespace cutum

#endif // WINDOWMANAGER_H
