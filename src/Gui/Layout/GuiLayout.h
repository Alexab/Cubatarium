#pragma once

#include "Gui/Core/GuiTypes.h"
#include <algorithm>
#include <vector>

namespace cutum
{

class UGuiWidget;

enum class GuiAnchorKind
{
  TopLeft,
  TopCenter,
  Center,
  BottomCenter,
  Fill
};

struct HotbarLayoutResult
{
  int startX{0};
  int blockRowY{0};
  int prefabRowY{0};
  int totalW{0};
};

struct GuiGridSpec
{
  int columns{1};
  int hGap{8};
  int vGap{6};
  int Padding{0};
  std::vector<int> columnWeights;
};

struct GuiGridItem
{
  UGuiWidget *widget{nullptr};
  int row{0};
  int col{0};
  int rowSpan{1};
  int colSpan{1};
  int minH{0};

  GuiGridItem() = default;
  GuiGridItem(UGuiWidget *w, int r, int c, int rs = 1, int cs = 1, int mh = 0)
      : widget(w), row(r), col(c), rowSpan(rs), colSpan(cs), minH(mh)
  {
  }
};

HotbarLayoutResult LayoutHotbarRows(int viewportW, int viewportH, int slotSize,
                                    int gap, int marginBottom);

class UGuiLayout
{
public:
  static void StackVertical(const GuiRect &clientArea, int spacing, int Padding,
                            const std::vector<UGuiWidget *> &children);
  /// Returns total height used (including Padding).
  static int StackVerticalMeasure(const GuiRect &clientArea, int spacing,
                                  int Padding,
                                  const std::vector<UGuiWidget *> &children);
  static void StackHorizontal(const GuiRect &clientArea, int spacing,
                              int Padding,
                              const std::vector<UGuiWidget *> &children);
  static void AnchorChild(const GuiRect &clientArea, GuiAnchorKind kind,
                          int margin, UGuiWidget *child);
  static int GridMeasure(const GuiRect &clientArea, const GuiGridSpec &spec,
                         const std::vector<GuiGridItem> &items);
  static void GridPlace(const GuiRect &clientArea, const GuiGridSpec &spec,
                        const std::vector<GuiGridItem> &items);
};

inline bool StackIsUnlimited(int count)
{
  return count < 0;
}

/// How many slot columns fit. maxCols <= 0 means "as many as the width allows".
inline int SlotGridColumnCount(int viewportW, int slotSize, int gap, int maxCols)
{
  const int denom = std::max(1, slotSize + gap);
  const int fit = std::max(1, (std::max(0, viewportW) + gap) / denom);
  if (maxCols > 0)
  {
    return std::min(maxCols, fit);
  }
  return fit;
}

/// Pick the closest enabled tab. Ties go to the lower index, never to the last
/// tab just because it was appended.
inline int NearestEnabledIndex(int previous, const std::vector<bool> &enabled)
{
  if (enabled.empty())
  {
    return 0;
  }
  if (previous >= 0 && previous < static_cast<int>(enabled.size()) &&
      enabled[static_cast<size_t>(previous)])
  {
    return previous;
  }
  int best = -1;
  int bestDist = 0;
  for (int i = 0; i < static_cast<int>(enabled.size()); ++i)
  {
    if (!enabled[static_cast<size_t>(i)])
    {
      continue;
    }
    const int dist = previous < 0 ? i : std::abs(i - previous);
    if (best < 0 || dist < bestDist || (dist == bestDist && i < best))
    {
      best = i;
      bestDist = dist;
    }
  }
  return best < 0 ? 0 : best;
}

} // namespace cutum
