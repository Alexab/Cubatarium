#include "GuiLayout.h"
#include "Gui/Widgets/GuiWidget.h"

#include <algorithm>

namespace cutum {

namespace {

GuiRect ClientWithPadding(const GuiRect& area, int padding)
{
    return area.Inset(padding);
}

} // namespace

void GuiLayout::StackVertical(const GuiRect& clientArea, int spacing, int padding,
                             const std::vector<GuiWidget*>& children)
{
    GuiRect cursor = ClientWithPadding(clientArea, padding);
    for (GuiWidget* child : children) {
        if (!child || !child->IsVisible()) {
            continue;
        }
        const int childH = child->GetPreferredHeight();
        child->SetBounds({cursor.x, cursor.y, cursor.w, childH});
        cursor.y += childH + spacing;
    }
}

void GuiLayout::StackHorizontal(const GuiRect& clientArea, int spacing, int padding,
                               const std::vector<GuiWidget*>& children)
{
    GuiRect cursor = ClientWithPadding(clientArea, padding);
    for (GuiWidget* child : children) {
        if (!child || !child->IsVisible()) {
            continue;
        }
        const int childW = child->GetPreferredWidth();
        child->SetBounds({cursor.x, cursor.y, childW, cursor.h});
        cursor.x += childW + spacing;
    }
}

void GuiLayout::AnchorChild(const GuiRect& clientArea, GuiAnchorKind kind, int margin,
                            GuiWidget* child)
{
    if (!child) {
        return;
    }
    const int pw = child->GetPreferredWidth();
    const int ph = child->GetPreferredHeight();
    GuiRect bounds;
    switch (kind) {
    case GuiAnchorKind::Fill:
        bounds = clientArea.Inset(margin);
        break;
    case GuiAnchorKind::Center:
        bounds = {clientArea.x + (clientArea.w - pw) / 2,
                  clientArea.y + (clientArea.h - ph) / 2, pw, ph};
        break;
    case GuiAnchorKind::BottomCenter:
        bounds = {clientArea.x + (clientArea.w - pw) / 2,
                  clientArea.y + clientArea.h - ph - margin, pw, ph};
        break;
    case GuiAnchorKind::TopLeft:
    default:
        bounds = {clientArea.x + margin, clientArea.y + margin, pw, ph};
        break;
    }
    child->SetBounds(bounds);
}

} // namespace cutum
