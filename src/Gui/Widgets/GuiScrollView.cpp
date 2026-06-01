#include "GuiScrollView.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

#include <algorithm>

namespace cutum {

GuiScrollView::GuiScrollView(const GuiTheme* theme)
    : theme_(theme)
{
}

void GuiScrollView::SetContent(GuiWidget* content)
{
    content_ = content;
}

void GuiScrollView::Draw(GuiRenderer& renderer)
{
    if (!visible_ || !content_) {
        return;
    }
    renderer.PushClipRect(bounds_);
    const GuiRect shifted{bounds_.x, bounds_.y - scrollY_, bounds_.w, bounds_.h + scrollY_};
    content_->SetBounds(shifted);
    content_->Draw(renderer);
    renderer.PopClipRect();
}

bool GuiScrollView::OnScroll(const GuiScrollEvent& event)
{
    if (!visible_) {
        return false;
    }
    scrollY_ -= static_cast<int>(event.yoffset * 24);
    scrollY_ = std::max(0, scrollY_);
    return true;
}

} // namespace cutum
