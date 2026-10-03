#pragma once

#include "Gui/Core/GuiTypes.h"
#include <string>

namespace cutum
{

struct GuiTheme;
struct ItemDefinition;
class UGuiLabel;
class UGuiRenderer;

void LayoutTooltipNearPointer(UGuiLabel &label, const std::string &text,
                              int pointerX, int pointerY,
                              const GuiRect &viewport, const GuiTheme &theme,
                              int measuredTextWidth = -1);

int MeasureTooltipTextWidth(const std::string &text, const GuiTheme &theme,
                            UGuiRenderer *renderer);

/// Append damage / dig / armor / durability lines under display name.
std::string BuildItemTooltipText(const std::string &displayName,
                                 const ItemDefinition *def, float wear01 = 0.f,
                                 bool broken = false);

} // namespace cutum
