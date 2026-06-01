#ifndef GUI_SLOT_H
#define GUI_SLOT_H

#include "GuiWidget.h"
#include <functional>
#include <glm/glm.hpp>
#include <string>

namespace cutum {

struct GuiTheme;

class GuiSlot : public GuiWidget {
public:
    GuiSlot(const GuiTheme* theme, int size);

    void SetSelected(bool selected) { selected_ = selected; }
    void SetLabel(const std::string& label) { label_ = label; }
    void SetOnClick(std::function<void()> handler) { onClick_ = std::move(handler); }

    void Draw(GuiRenderer& renderer) override;
    bool OnMouseDown(const GuiMouseEvent& event) override;
    bool OnMouseUp(const GuiMouseEvent& event) override;

    int GetPreferredWidth() const override;
    int GetPreferredHeight() const override;

private:
    const GuiTheme* theme_;
    int slotSize_;
    bool selected_{false};
    std::string label_;
    bool pressed_{false};
    std::function<void()> onClick_;
};

} // namespace cutum

#endif
