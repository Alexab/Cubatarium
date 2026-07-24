#ifndef GUILISTSCROLLMIXIN_H
#define GUILISTSCROLLMIXIN_H

#include "Gui/Core/GuiTypes.h"
#include <algorithm>

namespace cutum
{

/// Shared scrollbar math for list widgets (GuiListView, GuiCheckList).
struct GuiListScrollMixin
{
  static int MaxScrollY(int contentHeightPx, int viewportHeightPx)
  {
    return std::max(0, contentHeightPx - viewportHeightPx);
  }

  static void ClampScroll(int &scrollOffsetPx, int maxScrollY)
  {
    scrollOffsetPx = std::clamp(scrollOffsetPx, 0, maxScrollY);
  }

  static GuiRect ScrollbarTrackRect(const GuiRect &bounds, int scrollbarWidthPx,
                                    int maxScrollY)
  {
    if (maxScrollY <= 0)
    {
      return {};
    }
    return GuiRect{bounds.X + bounds.W - scrollbarWidthPx, bounds.Y,
                   scrollbarWidthPx, bounds.H};
  }

  static GuiRect ScrollbarThumbRect(const GuiRect &track, int scrollOffsetPx,
                                    int maxScrollY, int thumbMinHeightPx)
  {
    if (track.W <= 0 || track.H <= 0 || maxScrollY <= 0)
    {
      return {};
    }
    const int thumbH =
        std::max(thumbMinHeightPx, track.H * track.H / (track.H + maxScrollY));
    const int thumbY =
        track.Y + (scrollOffsetPx * (track.H - thumbH)) / maxScrollY;
    return GuiRect{track.X, thumbY, track.W, thumbH};
  }

  static void ScrollRowIntoView(int rowTopPx, int rowBottomPx,
                                int viewportHeightPx, int &scrollOffsetPx)
  {
    if (rowTopPx < scrollOffsetPx)
    {
      scrollOffsetPx = rowTopPx;
    }
    else if (rowBottomPx > scrollOffsetPx + viewportHeightPx)
    {
      scrollOffsetPx = rowBottomPx - viewportHeightPx;
    }
  }
};

} // namespace cutum

#endif
