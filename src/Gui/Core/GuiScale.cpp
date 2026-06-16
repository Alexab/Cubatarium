#include "Gui/Core/GuiScale.h"

#include "Gui/Core/GuiTheme.h"

#include <algorithm>
#include <cmath>

namespace cutum
{

float ComputeUiScale(int densityDpi)
{
  return ComputeUiScale(densityDpi, 0, 0);
}

float ComputeUiScale(int densityDpi, int screenWidthPx, int screenHeightPx)
{
  if (densityDpi <= 0 && screenWidthPx <= 0 && screenHeightPx <= 0)
  {
    return 1.f;
  }

  const float dpiFactor =
      densityDpi > 0
          ? std::clamp(static_cast<float>(densityDpi) / kGuiMobileBaselineDpi,
                       1.f, 2.5f)
          : 1.f;

  float scale = dpiFactor;
  if (screenWidthPx > 0 && screenHeightPx > 0)
  {
    const int shortEdge = std::min(screenWidthPx, screenHeightPx);
    const float screenFactor =
        std::clamp(static_cast<float>(shortEdge) / 720.f, 0.55f, 1.15f);
    scale *= screenFactor;

    // Keep touch targets near ~10% of the short edge (base Button = 64px).
    const float maxByScreen = (static_cast<float>(shortEdge) * 0.10f) / 64.f;
    scale = std::min(scale, maxByScreen);
  }

  return std::clamp(scale, 1.f, 2.25f);
}

int ScalePx(int value, float scale)
{
  return static_cast<int>(std::lround(static_cast<float>(value) * scale));
}

GuiTheme ScaleGuiTheme(const GuiTheme &base, float scale)
{
  if (scale <= 1.f + 1e-4f)
  {
    return base;
  }
  GuiTheme scaled = base;
  scaled.FontSizeBody = ScalePx(base.FontSizeBody, scale);
  scaled.Padding = ScalePx(base.Padding, scale);
  scaled.HotbarSlotSize = ScalePx(base.HotbarSlotSize, scale);
  scaled.HotbarSlotGap = ScalePx(base.HotbarSlotGap, scale);
  scaled.BorderThickness = std::max(1, ScalePx(base.BorderThickness, scale));
  scaled.FocusRingThickness =
      std::max(1, ScalePx(base.FocusRingThickness, scale));
  scaled.SlotSelectedBorderThickness =
      std::max(1, ScalePx(base.SlotSelectedBorderThickness, scale));
  return scaled;
}

} // namespace cutum
