#include "Gui/Core/GuiScale.h"

#include "Gui/Core/GuiMetrics.h"
#include "Gui/Core/GuiTheme.h"

#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

int ScaleThemeInt(int base, float scale, int min_value = 1)
{
  if (std::fabs(scale - 1.f) < 1e-4f)
  {
    return base;
  }
  return std::max(min_value, ScalePx(base, scale));
}

} // namespace

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

    const float maxByScreen =
        (static_cast<float>(shortEdge) * 0.10f) / 64.f;
    scale = std::min(scale, maxByScreen);
  }

  return std::clamp(scale, 1.f, 2.25f);
}

float ComputeDesktopUiScale(float contentScaleX, float contentScaleY,
                            int screenWidthPx, int screenHeightPx)
{
  const float content_scale =
      (contentScaleX > 0.f && contentScaleY > 0.f)
          ? (contentScaleX + contentScaleY) * 0.5f
          : 1.f;

  float scale = std::clamp(content_scale, 0.75f, 2.5f);
  if (screenWidthPx > 0 && screenHeightPx > 0)
  {
    const int shortEdge = std::min(screenWidthPx, screenHeightPx);
    const float screenFactor =
        std::clamp(static_cast<float>(shortEdge) / 1080.f, 0.65f, 1.25f);
    scale *= screenFactor;
  }

  return std::clamp(scale, 0.75f, 2.25f);
}

float ResolveEffectiveUiScale(float user_scale,
                              const PlatformUiMetrics &platform)
{
  const float clamped_user =
      std::clamp(user_scale, kGuiMinUserScale, kGuiMaxUserScale);

  float auto_scale = 1.f;
#if defined(__ANDROID__)
  auto_scale = ComputeUiScale(platform.DensityDpi, platform.ScreenWidthPx,
                              platform.ScreenHeightPx);
#else
  auto_scale =
      ComputeDesktopUiScale(platform.ContentScaleX, platform.ContentScaleY,
                          platform.ScreenWidthPx, platform.ScreenHeightPx);
#endif

  return std::clamp(auto_scale * clamped_user, kGuiMinEffectiveScale,
                    kGuiMaxEffectiveScale);
}

int ScalePx(int value, float scale)
{
  return static_cast<int>(std::lround(static_cast<float>(value) * scale));
}

GuiTheme ScaleGuiTheme(const GuiTheme &base, float scale)
{
  if (std::fabs(scale - 1.f) < 1e-4f)
  {
    return base;
  }

  GuiTheme scaled = base;
  scaled.FontSizeBody = ScalePx(base.FontSizeBody, scale);
  scaled.Padding = ScalePx(base.Padding, scale);
  scaled.HotbarSlotSize = ScalePx(base.HotbarSlotSize, scale);
  scaled.HotbarSlotGap = ScalePx(base.HotbarSlotGap, scale);
  scaled.BorderThickness = ScaleThemeInt(base.BorderThickness, scale);
  scaled.FocusRingThickness = ScaleThemeInt(base.FocusRingThickness, scale);
  scaled.SlotSelectedBorderThickness =
      ScaleThemeInt(base.SlotSelectedBorderThickness, scale);
  scaled.ProgressBarHeight = ScaleThemeInt(base.ProgressBarHeight, scale);
  scaled.TitleBarHeight = ScaleThemeInt(base.TitleBarHeight, scale);
  scaled.TabBarHeight = ScaleThemeInt(base.TabBarHeight, scale);
  scaled.FooterHeight = ScaleThemeInt(base.FooterHeight, scale);
  scaled.ScrollbarWidth = ScaleThemeInt(base.ScrollbarWidth, scale);
  scaled.TouchDragSlopPx = ScaleThemeInt(base.TouchDragSlopPx, scale);
  scaled.SlotDragThresholdPx = ScaleThemeInt(base.SlotDragThresholdPx, scale);
  scaled.SlotIconInset = ScaleThemeInt(base.SlotIconInset, scale, 1);
  scaled.HotbarMarginBottom = ScalePx(base.HotbarMarginBottom, scale);
  scaled.HotbarSecondaryMarginRight =
      ScalePx(base.HotbarSecondaryMarginRight, scale);
  scaled.HotbarSecondaryMarginBottom =
      ScalePx(base.HotbarSecondaryMarginBottom, scale);
  scaled.MenuButtonWidth = ScalePx(base.MenuButtonWidth, scale);
  scaled.MenuButtonHeight = ScalePx(base.MenuButtonHeight, scale);
  scaled.MenuButtonSpacing = ScalePx(base.MenuButtonSpacing, scale);
  scaled.TouchButtonSize = ScalePx(base.TouchButtonSize, scale);
  scaled.TouchJoystickSize = ScalePx(base.TouchJoystickSize, scale);
  scaled.TouchMargin = ScalePx(base.TouchMargin, scale);
  scaled.TouchButtonGap = ScalePx(base.TouchButtonGap, scale);
  scaled.TouchControlLift = ScalePx(base.TouchControlLift, scale);
  scaled.TouchTopRightStackOffset =
      ScalePx(base.TouchTopRightStackOffset, scale);
  scaled.DialogDefaultWidth = ScalePx(base.DialogDefaultWidth, scale);
  scaled.DialogDefaultHeight = ScalePx(base.DialogDefaultHeight, scale);
  scaled.DialogResourcePacksWidth =
      ScalePx(base.DialogResourcePacksWidth, scale);
  scaled.DialogResourcePacksHeight =
      ScalePx(base.DialogResourcePacksHeight, scale);
  scaled.DialogMargin = ScalePx(base.DialogMargin, scale);
  scaled.ContentPad = ScalePx(base.ContentPad, scale);
  return scaled;
}

} // namespace cutum
