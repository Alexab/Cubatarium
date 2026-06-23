#ifndef GUI_SCALE_H
#define GUI_SCALE_H

namespace cutum
{

struct GuiTheme;

/// Desktop reference density (~96 dpi at typical monitor distance).
constexpr float kGuiReferenceDpi = 96.f;
/// Mobile baseline density for touch UI sizing.
constexpr float kGuiMobileBaselineDpi = 160.f;

float ComputeUiScale(int densityDpi);
float ComputeUiScale(int densityDpi, int screenWidthPx, int screenHeightPx);
GuiTheme ScaleGuiTheme(const GuiTheme &base, float scale);
int ScalePx(int value, float scale);

} // namespace cutum

#endif
