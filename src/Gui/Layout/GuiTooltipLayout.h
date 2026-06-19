#pragma once

#include "Gui/Core/GuiTypes.h"
#include <string>

namespace cutum
{

struct GuiTheme;
class UGuiLabel;
class UGuiRenderer;

void LayoutTooltipNearPointer(UGuiLabel &label, const std::string &text,
                              int pointerX, int pointerY,
                              const GuiRect &viewport, const GuiTheme &theme,
                              int measuredTextWidth = -1);

int MeasureTooltipTextWidth(const std::string &text, const GuiTheme &theme,
                            UGuiRenderer *renderer);

} // namespace cutum
