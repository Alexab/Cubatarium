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
    if (!visible_ || !theme_) {
        return;
    }
    renderer.DrawText(text_, bounds_.x + theme_->padding, bounds_.y + theme_->padding,
                      static_cast<float>(theme_->fontSizeBody), theme_->textPrimary);
}

} // namespace cutum
