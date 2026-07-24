#include "Gui/Core/GuiMetrics.h"

#include "Gui/Core/GuiScale.h"

namespace cutum
{

int GuiMetrics::Dp(int design_px) const
{
  return ScalePx(design_px, Scale);
}

int GuiMetrics::TouchSlop() const
{
  return Theme ? Theme->TouchDragSlopPx : 14;
}

bool GuiMetrics::IsNarrow(int viewport_w) const
{
  if (Scale <= 1e-4f)
  {
    return viewport_w < kNarrowWidthDp;
  }
  return static_cast<int>(static_cast<float>(viewport_w) / Scale) <
         kNarrowWidthDp;
}

GuiMetrics MakeGuiMetrics(float scale, const GuiTheme &theme)
{
  GuiMetrics metrics;
  metrics.Scale = scale;
  metrics.Theme = &theme;
  return metrics;
}

} // namespace cutum
