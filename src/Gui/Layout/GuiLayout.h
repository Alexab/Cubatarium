#pragma once

#include "Gui/Core/GuiTypes.h"
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

} // namespace cutum
