#ifndef GUI_THEME_H
#define GUI_THEME_H

#include <glm/glm.hpp>

namespace cutum {

struct GuiTheme {
    glm::vec4 panelBackground{0.1f, 0.1f, 0.12f, 0.85f};
    glm::vec4 panelBorder{0.4f, 0.4f, 0.45f, 1.0f};
    glm::vec4 buttonNormal{0.25f, 0.25f, 0.28f, 1.0f};
    glm::vec4 buttonHover{0.35f, 0.35f, 0.38f, 1.0f};
    glm::vec4 buttonPressed{0.18f, 0.18f, 0.2f, 1.0f};
    glm::vec4 buttonDisabled{0.15f, 0.15f, 0.16f, 0.6f};
    glm::vec3 textPrimary{1.0f, 1.0f, 1.0f};
    glm::vec3 textSecondary{0.75f, 0.75f, 0.8f};
    glm::vec4 slotBackground{0.2f, 0.2f, 0.22f, 0.9f};
    glm::vec4 slotSelected{0.75f, 0.88f, 0.28f, 1.0f};
    glm::vec4 slotSelectedFill{0.45f, 0.55f, 0.15f, 0.45f};
    glm::vec4 slotSelectedInner{0.9f, 0.95f, 0.5f, 0.9f};
    /// Keyboard focus ring (Tab navigation).
    glm::vec4 focusRing{0.75f, 0.88f, 0.28f, 1.0f};
    int focusRingThickness{2};
    int slotSelectedBorderThickness{3};
    int fontSizeBody{16};
    int padding{8};
    int hotbarSlotSize{48};
    int hotbarSlotGap{4};
    int borderThickness{1};
};

GuiTheme DefaultGuiTheme();

} // namespace cutum

#endif
