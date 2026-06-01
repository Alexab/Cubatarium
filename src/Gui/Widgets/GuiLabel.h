#ifndef GUI_LABEL_H
#define GUI_LABEL_H

#include "GuiWidget.h"
#include <string>

namespace cutum {

struct GuiTheme;

enum class GuiTextAlign {
    Left,
    Center
};

class GuiLabel : public GuiWidget {
public:
    GuiLabel(const GuiTheme* theme, std::string text);

    void SetText(const std::string& text) { text_ = text; }
    const std::string& GetText() const { return text_; }
    void SetTextAlign(GuiTextAlign align) { textAlign_ = align; }
    void SetDrawBackground(bool draw) { drawBackground_ = draw; }

    void Draw(GuiRenderer& renderer) override;

private:
    const GuiTheme* theme_;
    std::string text_;
    GuiTextAlign textAlign_{GuiTextAlign::Left};
    bool drawBackground_{false};
};

} // namespace cutum

#endif
