#include "GuiLabel.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

namespace cutum {

GuiLabel::GuiLabel(const GuiTheme* theme, std::string text)
    : theme_(theme)
    , text_(std::move(text))
{
}

void GuiLabel::Draw(GuiRenderer& renderer)
{
    if (!visible_ || !theme_ || text_.empty()) {
        return;
    }
    if (drawBackground_) {
        renderer.DrawFilledRect(bounds_, {0.05f, 0.05f, 0.08f, 0.75f});
    }
    if (textAlign_ == GuiTextAlign::Center) {
        renderer.DrawTextCenteredInRect(bounds_, text_, theme_->textPrimary);
    } else {
        renderer.DrawText(text_, bounds_.x + theme_->padding, bounds_.y + theme_->padding,
                          theme_->textPrimary);
    }
}

} // namespace cutum
