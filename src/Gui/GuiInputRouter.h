#ifndef GUI_INPUT_ROUTER_H
#define GUI_INPUT_ROUTER_H

#include "GuiTypes.h"
#include <vector>

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
    bool OnScroll(const GuiScrollEvent& event, int mouseX, int mouseY);

    bool WantsCaptureMouse() const;
    bool WantsCaptureKeyboard() const;

    void SetModalKeyboard(bool modal) { modalKeyboard_ = modal; }
    /// Сброс фокуса/захвата (обязательно при смене экрана — виджеты уничтожаются).
    void ClearInteractionState();
    /// Только обнулить указатели (после уничтожения виджетов при shutdown).
    void ReleaseFocusWithoutNotify();

private:
    void SetKeyboardFocus(GuiWidget* widget, bool reveal);
    void CollectFocusOrder();
    void FocusNext(bool reverse);

    GuiWidget* root_{nullptr};
    GuiScreenBase* screen_{nullptr};
    GuiWidget* keyboardFocus_{nullptr};
    GuiWidget* mousePressedWidget_{nullptr};
    GuiWidget* hoveredWidget_{nullptr};
    std::vector<GuiWidget*> focusOrder_;
    bool captureMouse_{false};
    bool modalKeyboard_{false};
    int lastMouseX_{-1};
    int lastMouseY_{-1};
};

} // namespace cutum

#endif
