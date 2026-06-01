#ifndef GUI_BUTTON_H
#define GUI_BUTTON_H

#include "GuiWidget.h"
#include <glm/glm.hpp>
#include <functional>
#include <string>

namespace cutum {

struct GuiTheme;

enum class GuiButtonState {
    Normal,
    Hovered,
    Pressed,
    Disabled
};

class GuiButton : public GuiWidget {
public:
    GuiButton(const GuiTheme* theme, std::string label);

    void SetOnClick(std::function<void()> handler) { onClick_ = std::move(handler); }
    void SetLabel(const std::string& label) { label_ = label; }

    void Draw(GuiRenderer& renderer) override;
    bool OnMouseDown(const GuiMouseEvent& event) override;
    bool OnMouseUp(const GuiMouseEvent& event) override;
    bool OnMouseMove(const GuiMouseEvent& event) override;

    int GetPreferredHeight() const override;

private:
    glm::vec4 StateColor() const;

    const GuiTheme* theme_;
    std::string label_;
    GuiButtonState state_{GuiButtonState::Normal};
    bool pressedInside_{false};
    std::function<void()> onClick_;
};

} // namespace cutum

#endif
