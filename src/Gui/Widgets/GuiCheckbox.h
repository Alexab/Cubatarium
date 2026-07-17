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
  void SetDescription(std::string description)
  {
    Description = std::move(description);
  }
  const std::string &GetDescription() const { return Description; }
  void SetOnDescriptionHover(std::function<void(const std::string &)> handler)
  {
    OnDescriptionHover = std::move(handler);
  }

  bool CanFocus() const override;
  bool Activate() override;

  void Draw(UGuiRenderer &renderer) override;
  bool OnMouseDown(const GuiMouseEvent &event) override;
  bool OnMouseMove(const GuiMouseEvent &event) override;

  int GetPreferredHeight() const override;

private:
  const GuiTheme *Theme;
  std::string Label;
  bool Checked{false};
  bool Hovered{false};
  std::string Description;
  std::function<void(bool)> OnChanged;
  std::function<void(const std::string &)> OnDescriptionHover;
};

} // namespace cutum

#endif
