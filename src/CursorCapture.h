#ifndef CURSOR_CAPTURE_H
#define CURSOR_CAPTURE_H

struct GLFWwindow;

namespace cutum {

enum class AppCursorPolicy {
    /// Обычный курсор (меню, UI-only).
    Free,
    /// Видимый курсор, не выходит за клиентскую область окна.
    ConfinedVisible,
};

void ApplyCursorPolicy(GLFWwindow* window, AppCursorPolicy policy);
void ReleasePlatformCursorClip();

} // namespace cutum

#endif
