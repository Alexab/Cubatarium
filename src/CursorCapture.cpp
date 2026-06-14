#include "CursorCapture.h"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace cutum
{

namespace
{

/// GLFW_CURSOR_CAPTURED was added in GLFW 3.4; older linked libraries reject
/// the value.
bool GlfwSupportsCursorCapturedMode()
{
#ifdef GLFW_CURSOR_CAPTURED
  int major = 0;
  int minor = 0;
  int rev = 0;
  glfwGetVersion(&major, &minor, &rev);
  return major > 3 || (major == 3 && minor >= 4);
#else
  return false;
#endif
}

#ifdef _WIN32
bool ConfineCursorWin32(GLFWwindow *window)
{
  HWND hwnd = glfwGetWin32Window(window);
  if (!hwnd)
  {
    return false;
  }
  RECT client{};
  if (!GetClientRect(hwnd, &client))
  {
    return false;
  }
  POINT topLeft{client.left, client.top};
  POINT bottomRight{client.right, client.bottom};
  if (!ClientToScreen(hwnd, &topLeft) || !ClientToScreen(hwnd, &bottomRight))
  {
    return false;
  }
  RECT screenRect{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
  return ClipCursor(&screenRect) != FALSE;
}
#endif

bool TryGlfwCapturedCursor(GLFWwindow *window)
{
#ifdef GLFW_CURSOR_CAPTURED
  if (!GlfwSupportsCursorCapturedMode())
  {
    return false;
  }
  if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_CAPTURED)
  {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_CAPTURED);
  }
  return glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_CAPTURED;
#else
  (void)window;
  return false;
#endif
}

} // namespace

void ReleasePlatformCursorClip()
{
#ifdef _WIN32
  ClipCursor(nullptr);
#endif
}

void ApplyCursorPolicy(GLFWwindow *window, AppCursorPolicy policy)
{
  if (!window)
  {
    return;
  }

  if (policy == AppCursorPolicy::Free)
  {
    if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_NORMAL)
    {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    ReleasePlatformCursorClip();
    return;
  }

  if (policy == AppCursorPolicy::CapturedHidden)
  {
    if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
    if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
      TryGlfwCapturedCursor(window);
    }
    ReleasePlatformCursorClip();
    return;
  }

  // ConfinedVisible
  if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_NORMAL)
  {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }
#ifdef _WIN32
  ConfineCursorWin32(window);
#else
  (void)window;
#endif
}

} // namespace cutum
