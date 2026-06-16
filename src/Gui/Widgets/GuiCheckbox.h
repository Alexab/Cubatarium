#ifndef GUI_CHECKBOX_H
#define GUI_CHECKBOX_H

#include "Gui/Widgets/GuiWidget.h"
#include <functional>
#include <string>

namespace cutum
{

struct GuiTheme;

class UGuiCheckbox : public UGuiWidget
{
public:
  UGuiCheckbox(const GuiTheme *theme, std::string label);

  void SetChecked(bool checked) { Checked = checked; }
  bool IsChecked() const { return Checked; }
  void SetOnChanged(std::function<void(bool)> handler)
  {
    OnChanged = std::move(handler);
  }

  bool CanFocus() const override;
  bool Activate() override;

  void Draw(UGuiRenderer &renderer) override;
  bool OnMouseDown(const GuiMouseEvent &event) override;

  int GetPreferredHeight() const override;

private:
  const GuiTheme *Theme;
  std::string Label;
  bool Checked{false};
  std::function<void(bool)> OnChanged;
};

} // namespace cutum

#endif
