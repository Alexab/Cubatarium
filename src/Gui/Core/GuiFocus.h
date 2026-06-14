#pragma once

#include <vector>

namespace cutum
{

class UGuiRenderer;
class UGuiWidget;
struct GuiRect;
struct GuiTheme;

void DrawWidgetFocusRing(UGuiRenderer &renderer, const GuiTheme &theme,
                         const GuiRect &bounds);

/// Scroll parent viewports so @p widget is visible (Tab focus, etc.).
void RevealWidgetForKeyboardFocus(UGuiWidget *root, UGuiWidget *widget);

} // namespace cutum
