#include "App/Platform/CursorCapture.h"

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

void CursorWindowToFramebuffer(GLFWwindow *window, double window_x,
                               double window_y, double &out_fb_x,
                               double &out_fb_y)
{
  out_fb_x = window_x;
  out_fb_y = window_y;
  if (!window)
  {
    return;
  }
  int fb_w = 0;
  int fb_h = 0;
  int win_w = 0;
  int win_h = 0;
  glfwGetFramebufferSize(window, &fb_w, &fb_h);
  glfwGetWindowSize(window, &win_w, &win_h);
  if (win_w <= 0 || win_h <= 0 || fb_w <= 0 || fb_h <= 0)
  {
    return;
  }
  out_fb_x = window_x * (static_cast<double>(fb_w) / static_cast<double>(win_w));
  out_fb_y = window_y * (static_cast<double>(fb_h) / static_cast<double>(win_h));
}

void CenterWindowCursor(GLFWwindow *window)
{
  if (!window)
  {
    return;
  }
  int win_w = 0;
  int win_h = 0;
  glfwGetWindowSize(window, &win_w, &win_h);
  if (win_w <= 0 || win_h <= 0)
  {
    return;
  }
  glfwSetCursorPos(window, win_w * 0.5, win_h * 0.5);
}

void ApplyCursorPolicy(GLFWwindow *window, AppCursorPolicy policy)
{
  if (!window)
  {
    return;
  }

  const int previous = glfwGetInputMode(window, GLFW_CURSOR);
  const bool leavingCapture =
      previous == GLFW_CURSOR_DISABLED
#ifdef GLFW_CURSOR_CAPTURED
      || previous == GLFW_CURSOR_CAPTURED
#endif
      ;

  if (policy == AppCursorPolicy::Free)
  {
    if (previous != GLFW_CURSOR_NORMAL)
    {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    if (leavingCapture)
    {
      CenterWindowCursor(window);
    }
    ReleasePlatformCursorClip();
    return;
  }

  if (policy == AppCursorPolicy::CapturedHidden)
  {
    if (previous != GLFW_CURSOR_DISABLED)
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

  // ConfinedVisible — visible OS cursor for Cubatarium / isometric aim.
  if (previous != GLFW_CURSOR_NORMAL)
  {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }
  if (leavingCapture)
  {
    CenterWindowCursor(window);
  }
#ifdef _WIN32
  ConfineCursorWin32(window);
#else
  (void)window;
#endif
}

} // namespace cutum
