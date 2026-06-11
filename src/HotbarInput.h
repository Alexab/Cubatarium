#ifndef HOTBAR_INPUT_H
#define HOTBAR_INPUT_H

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace cutum {

/// Primary hotbar slot index from GLFW key (1->0 .. 9->8, 0->9). Returns -1 if not a hotbar key.
inline int PrimaryHotbarIndexFromGlfwKey(int glfwKey)
{
    if (glfwKey >= GLFW_KEY_1 && glfwKey <= GLFW_KEY_9) {
        return glfwKey - GLFW_KEY_1;
    }
    if (glfwKey == GLFW_KEY_0) {
        return 9;
    }
    return -1;
}

} // namespace cutum

#endif
