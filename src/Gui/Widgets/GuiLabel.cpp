#include "GuiLabel.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

namespace cutum {

UGuiLabel::UGuiLabel(const GuiTheme* theme, std::string text)
    : theme_(theme)
    , text_(std::move(text))
{
}

void UGuiLabel::Draw(UGuiRenderer& renderer)
{
    if (!visible_ || !theme_ || text_.empty()) {
        return;
    }
    if (drawBackground_) {
        renderer.DrawFilledRect(bounds_, theme_->tooltipBackground);
    }
    const glm::vec3& color =
        useSecondaryColor_ ? theme_->textSecondary : theme_->textPrimary;
    if (textAlign_ == GuiTextAlign::Center) {
        renderer.DrawTextCenteredInRect(bounds_, text_, color);
    } else {
        renderer.DrawText(text_, bounds_.x + theme_->padding, bounds_.y + theme_->padding, color);
    }
}

} // namespace cutum
