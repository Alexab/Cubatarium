#ifndef GUI_WIDGET_H
#define GUI_WIDGET_H

#include "Gui/Core/GuiTypes.h"
#include <memory>
#include <vector>

namespace cutum
{

class UGuiRenderer;

class UGuiWidget
{
public:
  virtual ~UGuiWidget() = default;

  virtual void SetBounds(const GuiRect &bounds) { Bounds = bounds; }
  const GuiRect &GetBounds() const { return Bounds; }

  void SetVisible(bool visible) { Visible = visible; }
  bool IsVisible() const { return Visible; }

  void SetEnabled(bool enabled) { Enabled = enabled; }
  bool IsEnabled() const { return Enabled; }

  void SetZOrder(int z) { ZOrder = z; }
  int GetZOrder() const { return ZOrder; }

  void SetClipChildren(bool clip) { ClipChildren = clip; }
  bool GetClipChildren() const { return ClipChildren; }

  virtual int GetPreferredWidth() const
  {
    return Bounds.W > 0 ? Bounds.W : 100;
  }
  virtual int GetPreferredHeight() const
  {
    return Bounds.H > 0 ? Bounds.H : 32;
  }

  virtual void UpdateLayout(const GuiRect &parentClientArea);
  virtual void Update(double dt);
  virtual void Draw(UGuiRenderer &renderer);

  /// Tab-stop widget (buttons, inputs, checkboxes, lists, …).
  virtual bool CanFocus() const { return false; }
  /// Enter / Space on focused widget.
  virtual bool Activate();
  void SetFocusHighlight(bool on) { FocusHighlight = on; }
  bool HasFocusHighlight() const { return FocusHighlight; }
  virtual void CollectFocusables(std::vector<UGuiWidget *> &out);

  virtual UGuiWidget *HitTest(int x, int y);
  /// Deepest focusable widget at point (for mouse focus).
  virtual UGuiWidget *HitTestFocusable(int x, int y);
  const std::vector<std::unique_ptr<UGuiWidget>> &GetChildren() const
  {
    return Children;
  }

  /// Route mouse wheel to widget under cursor (depth-first).
  virtual bool ScrollAtPoint(int x, int y, const GuiScrollEvent &event);

  UGuiWidget *AddChild(std::unique_ptr<UGuiWidget> child);
  void ClearChildren();

  virtual bool OnMouseDown(const GuiMouseEvent &event);
  virtual bool OnMouseUp(const GuiMouseEvent &event);
  virtual bool OnMouseMove(const GuiMouseEvent &event);
  virtual bool OnKey(const GuiKeyEvent &event);
  virtual bool OnChar(const GuiCharEvent &event);
  virtual bool OnScroll(const GuiScrollEvent &event);

protected:
  GuiRect Bounds{0, 0, 100, 32};
  bool Visible{true};
  bool Enabled{true};
  bool FocusHighlight{false};
  bool ClipChildren{false};
  int ZOrder{0};
  std::vector<std::unique_ptr<UGuiWidget>> Children;
};

} // namespace cutum

#endif
