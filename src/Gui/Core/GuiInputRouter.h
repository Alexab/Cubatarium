#ifndef GUI_INPUT_ROUTER_H
#define GUI_INPUT_ROUTER_H

#include "Gui/Core/GuiTypes.h"
#include <vector>

namespace cutum
{

class UGuiWidget;
class UGuiScreenBase;
class UGuiRenderer;

class UGuiInputRouter
{
public:
  void SetRoot(UGuiWidget *root);
  void SetActiveScreen(UGuiScreenBase *screen);
  void SetRenderer(UGuiRenderer *renderer) { Renderer = renderer; }

  bool OnMouseDown(const GuiMouseEvent &event);
  bool OnMouseUp(const GuiMouseEvent &event);
  bool OnMouseMove(const GuiMouseEvent &event);
  bool OnKey(const GuiKeyEvent &event);
  bool OnChar(const GuiCharEvent &event);
  bool OnScroll(const GuiScrollEvent &event, int mouseX, int mouseY);

  bool WantsCaptureMouse() const;
  bool WantsCaptureKeyboard() const;

  void SetModalKeyboard(bool modal) { ModalKeyboard = modal; }
  /// Сброс фокуса/захвата (обязательно при смене экрана — виджеты
  /// уничтожаются).
  void ClearInteractionState();
  /// Только обнулить указатели (после уничтожения виджетов при shutdown).
  void ReleaseFocusWithoutNotify();

private:
  void SetKeyboardFocus(UGuiWidget *widget, bool reveal);
  void CollectFocusOrder();
  void FocusNext(bool reverse);

  UGuiWidget *Root{nullptr};
  UGuiScreenBase *Screen{nullptr};
  UGuiRenderer *Renderer{nullptr};
  UGuiWidget *KeyboardFocus{nullptr};
  UGuiWidget *MousePressedWidget{nullptr};
  UGuiWidget *HoveredWidget{nullptr};
  std::vector<UGuiWidget *> FocusOrder;
  bool CaptureMouse{false};
  bool ModalKeyboard{false};
  int LastMouseX{-1};
  int LastMouseY{-1};
};

} // namespace cutum

#endif
