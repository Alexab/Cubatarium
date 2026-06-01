#include "GuiListView.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

#include <algorithm>

namespace cutum {

GuiListView::GuiListView(const GuiTheme* theme)
    : theme_(theme)
{
    if (theme_) {
        rowHeight_ = theme_->fontSizeBody + 4;
    }
}

void GuiListView::SetItems(std::vector<std::string> items)
{
    items_ = std::move(items);
    if (selectedIndex_ >= static_cast<int>(items_.size())) {
        selectedIndex_ = items_.empty() ? -1 : 0;
    }
}

void GuiListView::SetSelectedIndex(int index)
{
    selectedIndex_ = index;
}

void GuiListView::SetOnSelectionChanged(std::function<void(int)> handler)
{
    onSelectionChanged_ = std::move(handler);
}

void GuiListView::Draw(GuiRenderer& renderer)
{
    if (!visible_ || !theme_) {
        return;
    }
    renderer.PushClipRect(bounds_);
    renderer.DrawFilledRect(bounds_, theme_->buttonNormal);
    int y = bounds_.y - scrollOffsetPx_;
    for (size_t i = 0; i < items_.size(); ++i) {
        GuiRect row{bounds_.x, y, bounds_.w, rowHeight_};
        if (static_cast<int>(i) == selectedIndex_) {
            renderer.DrawFilledRect(row, theme_->buttonHover);
        }
        renderer.DrawText(items_[i], row.x + theme_->padding, row.y + 2, theme_->textPrimary);
        y += rowHeight_;
    }
    renderer.PopClipRect();
}

bool GuiListView::OnMouseDown(const GuiMouseEvent& event)
{
    if (!visible_ || !bounds_.Contains(event.x, event.y)) {
        return false;
    }
    const int localY = event.y - bounds_.y + scrollOffsetPx_;
    const int index = localY / rowHeight_;
    if (index >= 0 && index < static_cast<int>(items_.size())) {
        selectedIndex_ = index;
        if (onSelectionChanged_) {
            onSelectionChanged_(index);
        }
        return true;
    }
    return false;
}

bool GuiListView::OnScroll(const GuiScrollEvent& event)
{
    if (!visible_ || !bounds_.Contains(0, 0)) {
        return false;
    }
    scrollOffsetPx_ -= static_cast<int>(event.yoffset * rowHeight_);
    const int contentH = static_cast<int>(items_.size()) * rowHeight_;
    const int maxScroll = std::max(0, contentH - bounds_.h);
    scrollOffsetPx_ = std::clamp(scrollOffsetPx_, 0, maxScroll);
    return true;
}

} // namespace cutum
