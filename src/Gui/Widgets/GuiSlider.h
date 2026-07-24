#ifndef GUI_SLIDER_H
#define GUI_SLIDER_H

#include "Gui/Widgets/GuiWidget.h"
#include <functional>

namespace cutum
{

struct GuiTheme;

class UGuiSlider : public UGuiWidget
{
public:
  explicit UGuiSlider(const GuiTheme *theme);

  void SetRange(float min, float max);
  void SetValue(float value);
  float GetValue() const { return Value; }
  void SetStep(float step) { Step = step; }
  void SetOnValueChanged(std::function<void(float)> handler)
  {
    OnValueChanged = std::move(handler);
  }
  void SetOnCommit(std::function<void(float)> handler)
  {
    OnCommit = std::move(handler);
  }

  int GetPreferredHeight() const override;
  void Draw(UGuiRenderer &renderer) override;
  bool ConsumesScrollDragAt(int x, int y) const override;
  bool OnMouseDown(const GuiMouseEvent &event) override;
  bool OnMouseUp(const GuiMouseEvent &event) override;
  bool OnMouseMove(const GuiMouseEvent &event) override;

private:
  float ValueFromX(int x) const;
  GuiRect TrackRect() const;
  GuiRect ThumbRect() const;
  void ApplyValue(float value, bool notify);

  const GuiTheme *Theme;
  float Min{0.f};
  float Max{1.f};
  float Value{0.5f};
  float Step{0.05f};
  bool Dragging{false};
  std::function<void(float)> OnValueChanged;
  std::function<void(float)> OnCommit;
};

} // namespace cutum

#endif
