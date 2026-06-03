#include "GuiPopupMenu.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

#include <algorithm>

namespace cutum {

GuiPopupMenu::GuiPopupMenu(const GuiTheme* theme)
    : theme_(theme)
{
    SetVisible(false);
    SetZOrder(1000);
}

void GuiPopupMenu::SetItems(std::vector<GuiPopupMenuItem> items)
{
    items_ = std::move(items);
}

int GuiPopupMenu::ItemHeight() const
{
    return theme_ ? theme_->fontSizeBody + theme_->padding * 2 : 28;
}

int GuiPopupMenu::MenuWidth(int viewportW) const
{
    if (!theme_) {
        return 120;
    }
    int maxW = 80;
    for (const auto& item : items_) {
        const int w = theme_->padding * 2 + static_cast<int>(item.label.size()) * (theme_->fontSizeBody / 2 + 4);
        maxW = std::max(maxW, w);
    }
    return std::min(maxW, viewportW - 8);
}

void GuiPopupMenu::OpenAt(int x, int y, int viewportW, int viewportH)
{
    if (items_.empty() || !theme_) {
        return;
    }
    const int w = MenuWidth(viewportW);
    const int h = static_cast<int>(items_.size()) * ItemHeight();
    int px = x;
    int py = y;
    if (px + w > viewportW) {
        px = std::max(0, viewportW - w);
    }
    if (py + h > viewportH) {
        py = std::max(0, viewportH - h);
    }
    SetBounds({px, py, w, h});
    hoverIndex_ = -1;
    open_ = true;
    SetVisible(true);
}

void GuiPopupMenu::Close()
{
    open_ = false;
    SetVisible(false);
    hoverIndex_ = -1;
}

int GuiPopupMenu::ItemIndexAt(int x, int y) const
{
    if (!open_ || !bounds_.Contains(x, y)) {
        return -1;
    }
    const int localY = y - bounds_.y;
    const int idx = localY / ItemHeight();
    if (idx < 0 || idx >= static_cast<int>(items_.size())) {
        return -1;
    }
    return idx;
}

GuiWidget* GuiPopupMenu::HitTest(int x, int y)
{
    if (!open_ || !visible_) {
        return nullptr;
    }
    if (bounds_.Contains(x, y)) {
        return this;
    }
    return nullptr;
}

bool GuiPopupMenu::OnMouseMove(const GuiMouseEvent& event)
{
    if (!open_) {
        return false;
    }
    hoverIndex_ = ItemIndexAt(event.x, event.y);
    return bounds_.Contains(event.x, event.y);
}

bool GuiPopupMenu::OnMouseDown(const GuiMouseEvent& event)
{
    if (!open_ || event.button != GuiMouseButton::Left) {
        return false;
    }
    if (!bounds_.Contains(event.x, event.y)) {
        Close();
        return true;
    }
    const int idx = ItemIndexAt(event.x, event.y);
    if (idx >= 0 && idx < static_cast<int>(items_.size()) && items_[static_cast<size_t>(idx)].enabled) {
        if (items_[static_cast<size_t>(idx)].action) {
            items_[static_cast<size_t>(idx)].action();
        }
        Close();
        return true;
    }
    Close();
    return true;
}

void GuiPopupMenu::Draw(GuiRenderer& renderer)
{
    if (!open_ || !visible_ || !theme_) {
        return;
    }
    renderer.DrawFilledRect(bounds_, theme_->panelBackground);
    renderer.DrawBorderRect(bounds_, theme_->panelBorder, theme_->borderThickness);
    int y = bounds_.y;
    const int rowH = ItemHeight();
    for (size_t i = 0; i < items_.size(); ++i) {
        GuiRect row{bounds_.x, y, bounds_.w, rowH};
        if (static_cast<int>(i) == hoverIndex_) {
            renderer.DrawFilledRect(row, theme_->buttonHover);
        }
        const glm::vec3 color = items_[i].enabled ? theme_->textPrimary : theme_->textSecondary;
        renderer.DrawText(items_[i].label, row.x + theme_->padding, row.y + theme_->padding, color);
        y += rowH;
    }
}

} // namespace cutum
