#ifndef GUI_BUTTON_H
#define GUI_BUTTON_H

#include "GuiWidget.h"
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
    onClick_ = std::move(handler);
  }
  void SetLabel(const std::string &label) { label_ = label; }

  bool CanFocus() const override;
  bool Activate() override;

  void Draw(UGuiRenderer &renderer) override;
  bool OnMouseDown(const GuiMouseEvent &event) override;
  bool OnMouseUp(const GuiMouseEvent &event) override;
  bool OnMouseMove(const GuiMouseEvent &event) override;

  int GetPreferredHeight() const override;

private:
  glm::vec4 StateColor() const;

  const GuiTheme *theme_;
  std::string label_;
  GuiButtonState State{GuiButtonState::Normal};
  bool pressedInside_{false};
  int downX_{0};
  int downY_{0};
  bool dragged_{false};
  std::function<void()> onClick_;
};

} // namespace cutum

#endif
