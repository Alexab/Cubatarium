#include "Gui/Layout/GuiTooltipLayout.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Widgets/GuiLabel.h"

#include <algorithm>

namespace cutum
{

namespace
{

constexpr int kTooltipZOrder = 1000;
constexpr int kPointerOffsetX = 12;
constexpr int kPointerOffsetY = 16;
constexpr int kViewportMargin = 8;

int EstimateTextWidth(const std::string &text, const GuiTheme &theme)
{
  return static_cast<int>(text.size()) * (theme.FontSizeBody * 3 / 5);
}

} // namespace

int MeasureTooltipTextWidth(const std::string &text, const GuiTheme &theme,
                            UGuiRenderer *renderer)
{
  if (renderer)
  {
    return renderer->MeasureTextWidth(text);
  }
  return EstimateTextWidth(text, theme);
}

void LayoutTooltipNearPointer(UGuiLabel &label, const std::string &text,
                              int pointerX, int pointerY,
                              const GuiRect &viewport, const GuiTheme &theme,
                              int measuredTextWidth)
{
  if (text.empty())
  {
    label.SetVisible(false);
    return;
  }

  label.SetText(text);
  label.SetVisible(true);
  label.SetDrawBackground(true);
  label.SetTextAlign(GuiTextAlign::Left);
  label.SetZOrder(kTooltipZOrder);

  const int pad = theme.Padding * 2;
  const int h = theme.FontSizeBody + pad;
  const int textW = measuredTextWidth >= 0
                        ? measuredTextWidth
                        : EstimateTextWidth(text, theme);
  const int maxW = std::max(80, viewport.W - kViewportMargin * 2);
  const int w = std::min(maxW, textW + pad);

  int x = pointerX + kPointerOffsetX;
  int y = pointerY + kPointerOffsetY;

  if (x + w > viewport.X + viewport.W - kViewportMargin)
  {
    x = pointerX - w - kPointerOffsetX;
  }
  if (x < viewport.X + kViewportMargin)
  {
    x = viewport.X + kViewportMargin;
  }

  if (y + h > viewport.Y + viewport.H - kViewportMargin)
  {
    y = pointerY - h - kPointerOffsetY;
  }
  if (y < viewport.Y + kViewportMargin)
  {
    y = viewport.Y + kViewportMargin;
  }

  label.SetBounds({x, y, w, h});
}

} // namespace cutum
