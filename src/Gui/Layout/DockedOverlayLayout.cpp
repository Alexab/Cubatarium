#include "Gui/Layout/DockedOverlayLayout.h"
#include "Gui/Core/GuiTheme.h"
#include <algorithm>

namespace cutum
{

DockedLayout DockedOverlayLayout::Compute(int viewportW, int viewportH,
                                          int offsetX, int offsetY,
                                          int mainHeightPercent,
                                          int previewWidthPercent,
                                          const GuiTheme &theme)
{
  const int gap = theme.Padding;
  const int previewW =
      std::max(200, viewportW * std::max(20, previewWidthPercent) / 100);
  const int mainW = std::max(200, viewportW - previewW - gap);
  const int panelH =
      std::max(120, viewportH * std::max(50, mainHeightPercent) / 100);
  const int panelY = offsetY + (viewportH - panelH) / 2;

  DockedLayout layout;
  layout.main = {offsetX, panelY, mainW, panelH};
  layout.preview = {offsetX + mainW + gap, panelY, previewW, panelH};
  return layout;
}

} // namespace cutum
