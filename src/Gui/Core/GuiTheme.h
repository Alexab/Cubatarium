#ifndef GUI_THEME_H
#define GUI_THEME_H

#include <glm/glm.hpp>

namespace cutum
{

struct GuiTheme
{
  glm::vec4 PanelBackground{0.1f, 0.1f, 0.12f, 0.85f};
  glm::vec4 PanelBorder{0.4f, 0.4f, 0.45f, 1.0f};
  glm::vec4 ButtonNormal{0.25f, 0.25f, 0.28f, 1.0f};
  glm::vec4 ButtonHover{0.35f, 0.35f, 0.38f, 1.0f};
  glm::vec4 ButtonPressed{0.18f, 0.18f, 0.2f, 1.0f};
  glm::vec4 ButtonDisabled{0.15f, 0.15f, 0.16f, 0.6f};
  glm::vec3 TextPrimary{1.0f, 1.0f, 1.0f};
  glm::vec3 TextSecondary{0.75f, 0.75f, 0.8f};
  glm::vec4 SlotBackground{0.2f, 0.2f, 0.22f, 0.9f};
  glm::vec4 TooltipBackground{0.05f, 0.05f, 0.08f, 0.38f};
  glm::vec4 SlotSelected{0.75f, 0.88f, 0.28f, 1.0f};
  glm::vec4 SlotSelectedFill{0.45f, 0.55f, 0.15f, 0.45f};
  glm::vec4 SlotSelectedInner{0.9f, 0.95f, 0.5f, 0.9f};
  /// Keyboard focus ring (Tab navigation).
  glm::vec4 FocusRing{0.75f, 0.88f, 0.28f, 1.0f};
  int FocusRingThickness{2};
  int SlotSelectedBorderThickness{3};
  int FontSizeBody{16};
  int Padding{8};
  int HotbarSlotSize{48};
  int HotbarSlotGap{4};
  int BorderThickness{1};
};

GuiTheme DefaultGuiTheme();

} // namespace cutum

#endif
