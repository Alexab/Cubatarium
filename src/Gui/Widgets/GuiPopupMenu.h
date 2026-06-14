#ifndef GUI_POPUP_MENU_H
#define GUI_POPUP_MENU_H

#include "GuiWidget.h"
#include <functional>
#include <string>
#include <vector>

namespace cutum
{

struct GuiTheme;

struct GuiPopupMenuItem
{
  std::string label;
  std::function<void()> action;
  bool enabled{true};
};

class UGuiPopupMenu : public UGuiWidget
{
public:
  explicit UGuiPopupMenu(const GuiTheme *theme);

  void SetItems(std::vector<GuiPopupMenuItem> items);
  void OpenAt(int x, int y, int viewportW, int viewportH);
  void Close();
  bool IsOpen() const { return open_; }

  void Draw(UGuiRenderer &renderer) override;
  bool OnMouseDown(const GuiMouseEvent &event) override;
  bool OnMouseMove(const GuiMouseEvent &event) override;
  UGuiWidget *HitTest(int x, int y) override;

private:
  int ItemIndexAt(int x, int y) const;
  int ItemHeight() const;
  int MenuWidth(int viewportW) const;

  const GuiTheme *theme_;
  std::vector<GuiPopupMenuItem> items_;
  int hoverIndex_{-1};
  bool open_{false};
};

} // namespace cutum

#endif
