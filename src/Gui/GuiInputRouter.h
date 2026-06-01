#ifndef GUI_INPUT_ROUTER_H
#define GUI_INPUT_ROUTER_H

#include "GuiTypes.h"

namespace cutum {

class GuiWidget;
class GuiScreenBase;

class GuiInputRouter {
public:
    void SetRoot(GuiWidget* root);
    void SetActiveScreen(GuiScreenBase* screen);

    bool OnMouseDown(const GuiMouseEvent& event);
    bool OnMouseUp(const GuiMouseEvent& event);
    bool OnMouseMove(const GuiMouseEvent& event);
    bool OnKey(const GuiKeyEvent& event);
    bool OnChar(const GuiCharEvent& event);
    bool OnScroll(const GuiScrollEvent& event);

    bool WantsCaptureMouse() const;
    bool WantsCaptureKeyboard() const;

    void SetModalKeyboard(bool modal) { modalKeyboard_ = modal; }

private:
    GuiWidget* root_{nullptr};
    GuiScreenBase* screen_{nullptr};
    GuiWidget* focusedWidget_{nullptr};
    GuiWidget* hoveredWidget_{nullptr};
    bool captureMouse_{false};
    bool modalKeyboard_{false};
};

} // namespace cutum

#endif
