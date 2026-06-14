#ifndef GUI_KEY_CODES_H
#define GUI_KEY_CODES_H

#if defined(__ANDROID__)
#include "App/Platform/GlfwKeyCompat.h"
#else
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

namespace cutum
{
namespace GuiKey
{

inline constexpr int Tab = GLFW_KEY_TAB;
inline constexpr int Enter = GLFW_KEY_ENTER;
inline constexpr int KpEnter = GLFW_KEY_KP_ENTER;
inline constexpr int Space = GLFW_KEY_SPACE;
inline constexpr int Up = GLFW_KEY_UP;
inline constexpr int Down = GLFW_KEY_DOWN;
inline constexpr int Home = GLFW_KEY_HOME;
inline constexpr int End = GLFW_KEY_END;
inline constexpr int PageUp = GLFW_KEY_PAGE_UP;
inline constexpr int PageDown = GLFW_KEY_PAGE_DOWN;
inline constexpr int Delete = GLFW_KEY_DELETE;
inline constexpr int Backspace = GLFW_KEY_BACKSPACE;
inline constexpr int Left = GLFW_KEY_LEFT;
inline constexpr int Right = GLFW_KEY_RIGHT;
inline constexpr int ModShift = GLFW_MOD_SHIFT;
inline constexpr int ModControl = GLFW_MOD_CONTROL;
inline constexpr int ModAlt = GLFW_MOD_ALT;
inline constexpr int A = GLFW_KEY_A;
inline constexpr int C = GLFW_KEY_C;
inline constexpr int V = GLFW_KEY_V;
inline constexpr int X = GLFW_KEY_X;
inline constexpr int Z = GLFW_KEY_Z;
inline constexpr int Digit0 = GLFW_KEY_0;
inline constexpr int Digit9 = GLFW_KEY_9;

} // namespace GuiKey
} // namespace cutum

#endif
