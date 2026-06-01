#ifndef GUI_LABEL_H
#define GUI_LABEL_H

#include "GuiWidget.h"
#include <string>

namespace cutum {

struct GuiTheme;

class GuiLabel : public GuiWidget {
public:
    GuiLabel(const GuiTheme* theme, std::string text);

    void SetText(const std::string& text) { text_ = text; }
    const std::string& GetText() const { return text_; }

    void Draw(GuiRenderer& renderer) override;

private:
    const GuiTheme* theme_;
    std::string text_;
};

} // namespace cutum

#endif
