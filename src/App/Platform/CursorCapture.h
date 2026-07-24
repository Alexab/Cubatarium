#ifndef CURSOR_CAPTURE_H
#define CURSOR_CAPTURE_H

struct GLFWwindow;

namespace cutum
{

enum class AppCursorPolicy
{
  /// Обычный курсор (меню, UI-only).
  Free,
  /// Видимый курсор, не выходит за клиентскую область окна (Cubatarium).
  ConfinedVisible,
  /// Скрытый курсор, относительное движение (classic controls).
  CapturedHidden,
};

void ApplyCursorPolicy(GLFWwindow *window, AppCursorPolicy policy);
void ReleasePlatformCursorClip();
/// GLFW window coords → framebuffer pixels (DPI / content scale).
void CursorWindowToFramebuffer(GLFWwindow *window, double window_x,
                               double window_y, double &out_fb_x,
                               double &out_fb_y);
/// Place OS cursor at window center (after leaving DISABLED capture).
void CenterWindowCursor(GLFWwindow *window);

} // namespace cutum

#endif
