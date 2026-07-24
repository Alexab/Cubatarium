#ifndef GUI_BUTTON_H
#define GUI_BUTTON_H

#include "Gui/Widgets/GuiWidget.h"
#include <functional>
#include <glm/glm.hpp>
#include <string>

namespace cutum
{

struct GuiTheme;

enum class GuiButtonState
{
  Normal,
  Hovered,
  Pressed,
  Disabled
};

class UGuiButton : public UGuiWidget
{
public:
  UGuiButton(const GuiTheme *theme, std::string label);

  void SetOnClick(std::function<void()> handler)
  {
    OnClick = std::move(handler);
  }
  void SetLabel(const std::string &label) { Label = label; }

  bool CanFocus() const override;
  bool Activate() override;

  void Draw(UGuiRenderer &renderer) override;
  bool OnMouseDown(const GuiMouseEvent &event) override;
  bool OnMouseUp(const GuiMouseEvent &event) override;
  bool OnMouseMove(const GuiMouseEvent &event) override;

  int GetPreferredHeight() const override;

private:
  glm::vec4 StateColor() const;

  const GuiTheme *Theme;
  std::string Label;
  GuiButtonState State{GuiButtonState::Normal};
  bool PressedInside{false};
  int DownX{0};
  int DownY{0};
  bool Dragged{false};
  std::function<void()> OnClick;
};

} // namespace cutum

#endif
