#ifndef GUI_POPUP_MENU_H
#define GUI_POPUP_MENU_H

#include "Gui/Widgets/GuiWidget.h"
#include <functional>
#include <string>
#include <vector>

namespace cutum
{

struct GuiTheme;

struct GuiPopupMenuItem
{
  std::string label;
  std::function<void()> Action;
  bool enabled{true};
};

class UGuiPopupMenu : public UGuiWidget
{
public:
  explicit UGuiPopupMenu(const GuiTheme *theme);

  void SetItems(std::vector<GuiPopupMenuItem> items);
  void OpenAt(int x, int y, int viewportW, int viewportH);
  void Close();
  bool IsOpen() const { return Open; }

  void Draw(UGuiRenderer &renderer) override;
  bool OnMouseDown(const GuiMouseEvent &event) override;
  bool OnMouseMove(const GuiMouseEvent &event) override;
  UGuiWidget *HitTest(int x, int y) override;

private:
  int ItemIndexAt(int x, int y) const;
  int ItemHeight() const;
  int MenuWidth(int viewportW) const;

  const GuiTheme *Theme;
  std::vector<GuiPopupMenuItem> Items;
  int HoverIndex{-1};
  bool Open{false};
};

} // namespace cutum

#endif
