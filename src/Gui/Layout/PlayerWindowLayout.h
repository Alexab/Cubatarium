#pragma once
#include "Gui/Core/GuiMetrics.h"
#include "Gui/Core/GuiTypes.h"
#include <algorithm>
namespace cutum
{
struct PlayerWindowLayout
{
  GuiRect Window, Tabs, Body, Preview, Content, Footer;
  int SlotSize{48};
  static PlayerWindowLayout Compute(int width, int height,
                                    const GuiMetrics &metrics, int offsetX = 0,
                                    int offsetY = 0)
  {
    const auto &theme = *metrics.Theme;
    const int pad = metrics.Dp(8);
    const int w = std::max(300, std::min(width - pad * 2, metrics.Dp(1080)));
    const int h = std::max(300, std::min(height - pad * 2, metrics.Dp(660)));
    PlayerWindowLayout r;
    r.Window = {offsetX + (width - w) / 2, offsetY + (height - h) / 2, w, h};
    const int x = r.Window.X + pad, y = r.Window.Y + theme.TitleBarHeight;
    r.Tabs = {x, y + pad, w - pad * 2, metrics.Dp(32)};
    r.SlotSize =
        std::max(24, std::min({theme.HotbarSlotSize, height / 12,
                               (w - pad * 2 - theme.HotbarSlotGap * 9) / 10}));
    r.Footer = {x, r.Window.Y + h - pad - r.SlotSize, w - pad * 2, r.SlotSize};
    const int top = r.Tabs.Y + r.Tabs.H + pad;
    r.Body = {x, top, w - pad * 2, std::max(100, r.Footer.Y - pad - top)};
    const int preview = std::min(metrics.Dp(320), (r.Body.W - pad) / 3);
    r.Preview = {x, top, preview, r.Body.H};
    r.Content = {x + preview + pad, top, r.Body.W - preview - pad, r.Body.H};
    return r;
  }
};
} // namespace cutum
