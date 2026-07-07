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
  int previewW =
      std::max(200, viewportW * std::max(20, previewWidthPercent) / 100);
  // On narrow/short viewports, use single-pane mode so right-side controls
  // remain accessible above overlays.
  if (viewportW < 1100 || viewportH < 700)
  {
    previewW = 0;
  }
  const int mainW = std::max(200, viewportW - previewW - gap);
  const int panelH =
      std::max(120, viewportH * std::max(50, mainHeightPercent) / 100);
  const int panelY = offsetY + (viewportH - panelH) / 2;

  DockedLayout layout;
  layout.main = {offsetX, panelY, mainW, panelH};
  if (previewW > 0)
  {
    layout.preview = {offsetX + mainW + gap, panelY, previewW, panelH};
  }
  else
  {
    layout.preview = {offsetX + mainW, panelY, 0, panelH};
  }
  return layout;
}

} // namespace cutum
