#ifndef DOCKED_OVERLAY_LAYOUT_H
#define DOCKED_OVERLAY_LAYOUT_H

#include "Gui/Core/GuiTypes.h"

namespace cutum
{

struct GuiTheme;

struct DockedLayout
{
  GuiRect main;
  GuiRect preview;
};

class DockedOverlayLayout
{
public:
  static DockedLayout Compute(int viewportW, int viewportH, int offsetX,
                              int offsetY, int mainHeightPercent,
                              int previewWidthPercent, const GuiTheme &theme);
};

} // namespace cutum

#endif
