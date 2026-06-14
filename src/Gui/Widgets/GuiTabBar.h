#ifndef GUI_TAB_BAR_H
#define GUI_TAB_BAR_H

#include "GuiWidget.h"
#include <functional>
#include <string>
#include <vector>

namespace cutum
{

struct GuiTheme;

class UGuiTabBar : public UGuiWidget
{
public:
  UGuiTabBar(const GuiTheme *theme);

  void SetTabs(std::vector<std::string> labels);
  int GetActiveTab() const { return activeTab_; }
  void SetActiveTab(int tab);
  void SetOnTabChanged(std::function<void(int)> handler);

  void Draw(UGuiRenderer &renderer) override;
  bool OnMouseDown(const GuiMouseEvent &event) override;

  int GetPreferredHeight() const override;

private:
  const GuiTheme *theme_;
  std::vector<std::string> labels_;
  int activeTab_{0};
  std::function<void(int)> onTabChanged_;
};

} // namespace cutum

#endif
