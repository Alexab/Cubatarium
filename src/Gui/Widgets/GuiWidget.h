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

  void SetBounds(const GuiRect &bounds) { bounds_ = bounds; }
  const GuiRect &GetBounds() const { return bounds_; }

  void SetVisible(bool visible) { visible_ = visible; }
  bool IsVisible() const { return visible_; }

  void SetEnabled(bool enabled) { enabled_ = enabled; }
  bool IsEnabled() const { return enabled_; }

  void SetZOrder(int z) { zOrder_ = z; }
  int GetZOrder() const { return zOrder_; }

  virtual int GetPreferredWidth() const
  {
    return bounds_.w > 0 ? bounds_.w : 100;
  }
  virtual int GetPreferredHeight() const
  {
    return bounds_.h > 0 ? bounds_.h : 32;
  }

  virtual void UpdateLayout(const GuiRect &parentClientArea);
  virtual void Update(double dt);
  virtual void Draw(UGuiRenderer &renderer);

  /// Tab-stop widget (buttons, inputs, checkboxes, lists, …).
  virtual bool CanFocus() const { return false; }
  /// Enter / Space on focused widget.
  virtual bool Activate();
  void SetFocusHighlight(bool on) { focusHighlight_ = on; }
  bool HasFocusHighlight() const { return focusHighlight_; }
  virtual void CollectFocusables(std::vector<UGuiWidget *> &out);

  virtual UGuiWidget *HitTest(int x, int y);
  /// Deepest focusable widget at point (for mouse focus).
  virtual UGuiWidget *HitTestFocusable(int x, int y);
  const std::vector<std::unique_ptr<UGuiWidget>> &GetChildren() const
  {
    return children_;
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
  GuiRect bounds_{0, 0, 100, 32};
  bool visible_{true};
  bool enabled_{true};
  bool focusHighlight_{false};
  int zOrder_{0};
  std::vector<std::unique_ptr<UGuiWidget>> children_;
};

} // namespace cutum

#endif
