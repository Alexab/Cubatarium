#ifndef GUI_CHECKBOX_H
#define GUI_CHECKBOX_H

#include "GuiWidget.h"
#include <functional>
#include <string>

namespace cutum {

struct GuiTheme;

class UGuiCheckbox : public UGuiWidget {
public:
    UGuiCheckbox(const GuiTheme* theme, std::string label);

    void SetChecked(bool checked) { checked_ = checked; }
    bool IsChecked() const { return checked_; }
    void SetOnChanged(std::function<void(bool)> handler) { onChanged_ = std::move(handler); }

    bool CanFocus() const override;
    bool Activate() override;

    void Draw(UGuiRenderer& renderer) override;
    bool OnMouseDown(const GuiMouseEvent& event) override;

    int GetPreferredHeight() const override;

private:
    const GuiTheme* theme_;
    std::string label_;
    bool checked_{false};
    std::function<void(bool)> onChanged_;
};

} // namespace cutum

#endif
