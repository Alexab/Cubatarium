#pragma once

#include <vector>

namespace cutum {

class GuiRenderer;
class GuiWidget;
struct GuiRect;
struct GuiTheme;

void DrawWidgetFocusRing(GuiRenderer& renderer, const GuiTheme& theme, const GuiRect& bounds);

/// Scroll parent viewports so @p widget is visible (Tab focus, etc.).
void RevealWidgetForKeyboardFocus(GuiWidget* root, GuiWidget* widget);

} // namespace cutum
