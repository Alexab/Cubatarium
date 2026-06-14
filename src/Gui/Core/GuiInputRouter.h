#ifndef GUI_INPUT_ROUTER_H
#define GUI_INPUT_ROUTER_H

#include "Gui/Core/GuiTypes.h"
#include <vector>

namespace cutum
{

class UGuiWidget;
class UGuiScreenBase;

class UGuiInputRouter
{
public:
  void SetRoot(UGuiWidget *root);
  void SetActiveScreen(UGuiScreenBase *screen);

  bool OnMouseDown(const GuiMouseEvent &event);
  bool OnMouseUp(const GuiMouseEvent &event);
  bool OnMouseMove(const GuiMouseEvent &event);
  bool OnKey(const GuiKeyEvent &event);
  bool OnChar(const GuiCharEvent &event);
  bool OnScroll(const GuiScrollEvent &event, int mouseX, int mouseY);

  bool WantsCaptureMouse() const;
  bool WantsCaptureKeyboard() const;

  void SetModalKeyboard(bool modal) { modalKeyboard_ = modal; }
  /// Сброс фокуса/захвата (обязательно при смене экрана — виджеты
  /// уничтожаются).
  void ClearInteractionState();
  /// Только обнулить указатели (после уничтожения виджетов при shutdown).
  void ReleaseFocusWithoutNotify();

private:
  void SetKeyboardFocus(UGuiWidget *widget, bool reveal);
  void CollectFocusOrder();
  void FocusNext(bool reverse);

  UGuiWidget *root_{nullptr};
  UGuiScreenBase *screen_{nullptr};
  UGuiWidget *keyboardFocus_{nullptr};
  UGuiWidget *mousePressedWidget_{nullptr};
  UGuiWidget *hoveredWidget_{nullptr};
  std::vector<UGuiWidget *> focusOrder_;
  bool captureMouse_{false};
  bool modalKeyboard_{false};
  int lastMouseX_{-1};
  int lastMouseY_{-1};
};

} // namespace cutum

#endif
