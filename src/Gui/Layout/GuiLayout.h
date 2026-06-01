#ifndef GUI_LAYOUT_H
#define GUI_LAYOUT_H

#include "Gui/GuiTypes.h"
#include <vector>

namespace cutum {

class GuiWidget;

enum class GuiAnchorKind {
    TopLeft,
    Center,
    BottomCenter,
    Fill
};

class GuiLayout {
public:
    static void StackVertical(const GuiRect& clientArea, int spacing, int padding,
                              const std::vector<GuiWidget*>& children);
    static void StackHorizontal(const GuiRect& clientArea, int spacing, int padding,
                                const std::vector<GuiWidget*>& children);
    static void AnchorChild(const GuiRect& clientArea, GuiAnchorKind kind, int margin,
                            GuiWidget* child);
};

} // namespace cutum

#endif
