#ifndef GUI_METRICS_H
#define GUI_METRICS_H

#include "Gui/Core/GuiBreakpoints.h"
#include "Gui/Core/GuiTheme.h"

namespace cutum
{

struct PlatformUiMetrics
{
  int DensityDpi{0};
  float ContentScaleX{1.f};
  float ContentScaleY{1.f};
  int ScreenWidthPx{0};
  int ScreenHeightPx{0};
};

struct GuiMetrics
{
  float Scale{1.f};
  const GuiTheme *Theme{nullptr};

  int Dp(int design_px) const;
  int TouchSlop() const;
  bool IsNarrow(int viewport_w) const;
};

GuiMetrics MakeGuiMetrics(float scale, const GuiTheme &theme);

} // namespace cutum

#endif
