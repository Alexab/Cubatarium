#ifndef GUI_TEXT_INPUT_H
#define GUI_TEXT_INPUT_H

#include "GuiWidget.h"
#include <string>

namespace cutum {

struct GuiTheme;

class GuiTextInput : public GuiWidget {
public:
    GuiTextInput(const GuiTheme* theme);

    const std::string& GetText() const { return buffer_; }
    void SetText(const std::string& text);
    void SetFocused(bool focused) { focused_ = focused; }
    bool IsFocused() const { return focused_; }

    void Draw(GuiRenderer& renderer) override;
    bool OnMouseDown(const GuiMouseEvent& event) override;
    bool OnKey(const GuiKeyEvent& event) override;
    bool OnChar(const GuiCharEvent& event) override;

    int GetPreferredHeight() const override;

private:
    const GuiTheme* theme_;
    std::string buffer_;
    size_t caretPos_{0};
    bool focused_{false};
};

} // namespace cutum

#endif
