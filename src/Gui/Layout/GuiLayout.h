#ifndef GUI_LAYOUT_H
#define GUI_LAYOUT_H

#include "Gui/GuiTypes.h"
#include <vector>

namespace cutum {

class GuiWidget;

enum class GuiAnchorKind {
    TopLeft,
    TopCenter,
    Center,
    BottomCenter,
    Fill
};

struct HotbarLayoutResult {
    int startX{0};
    int blockRowY{0};
    int prefabRowY{0};
    int totalW{0};
};

/// Prefab row at bottom; block row above prefab.
HotbarLayoutResult LayoutHotbarRows(int viewportW, int viewportH, int slotSize, int gap,
                                    int marginBottom);

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
