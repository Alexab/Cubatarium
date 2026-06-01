#include "CursorCapture.h"

#include <GLFW/glfw3.h>

#ifndef GLFW_CURSOR_CAPTURED
#define GLFW_CURSOR_CAPTURED 0x00034006
#endif

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace cutum {

namespace {

#ifdef _WIN32
bool ConfineCursorWin32(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        return false;
    }
    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
        return false;
    }
    POINT topLeft{client.left, client.top};
    POINT bottomRight{client.right, client.bottom};
    if (!ClientToScreen(hwnd, &topLeft) || !ClientToScreen(hwnd, &bottomRight)) {
        return false;
    }
    RECT screenRect{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    return ClipCursor(&screenRect) != FALSE;
}
#endif

bool TryGlfwCapturedCursor(GLFWwindow* window)
{
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_CAPTURED);
    return glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_CAPTURED;
}

} // namespace

void ReleasePlatformCursorClip()
{
#ifdef _WIN32
    ClipCursor(nullptr);
#endif
}

void ApplyCursorPolicy(GLFWwindow* window, AppCursorPolicy policy)
{
    if (!window) {
        return;
    }

    if (policy == AppCursorPolicy::Free) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        ReleasePlatformCursorClip();
        return;
    }

    if (TryGlfwCapturedCursor(window)) {
        ReleasePlatformCursorClip();
        return;
    }

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
#ifdef _WIN32
    ConfineCursorWin32(window);
#else
    (void)window;
#endif
}

} // namespace cutum
