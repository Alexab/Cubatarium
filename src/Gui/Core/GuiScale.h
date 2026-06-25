#ifndef GUI_SCALE_H
#define GUI_SCALE_H

namespace cutum
{

struct GuiTheme;
struct PlatformUiMetrics;

/// Desktop reference density (~96 dpi at typical monitor distance).
constexpr float kGuiReferenceDpi = 96.f;
/// Mobile baseline density for touch UI sizing.
constexpr float kGuiMobileBaselineDpi = 160.f;
constexpr float kGuiMinEffectiveScale = 0.5f;
constexpr float kGuiMaxEffectiveScale = 3.0f;
constexpr float kGuiMinUserScale = 0.5f;
constexpr float kGuiMaxUserScale = 2.0f;

float ComputeUiScale(int densityDpi);
float ComputeUiScale(int densityDpi, int screenWidthPx, int screenHeightPx);
float ComputeDesktopUiScale(float contentScaleX, float contentScaleY,
                            int screenWidthPx, int screenHeightPx);
float ResolveEffectiveUiScale(float user_scale, const PlatformUiMetrics &platform);
GuiTheme ScaleGuiTheme(const GuiTheme &base, float scale);
int ScalePx(int value, float scale);

} // namespace cutum

#endif
