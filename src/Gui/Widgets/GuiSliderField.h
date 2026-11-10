#ifndef GUI_SLIDER_FIELD_H
#define GUI_SLIDER_FIELD_H

#include "Gui/Widgets/GuiWidget.h"
#include <functional>
#include <string>

namespace cutum
{

struct GuiTheme;
class UGuiSlider;
class UGuiTextInput;
class UGuiButton;

/// One settings row control: slider, numeric entry box and a "Reset" button
/// that restores the default value. Values are always clamped to the range and
/// snapped to the step; typing a number updates the slider immediately and the
/// text is normalised once the box loses focus.
class UGuiSliderField : public UGuiWidget
{
public:
  explicit UGuiSliderField(const GuiTheme *theme);

  /// `decimals` is the number of fractional digits shown (0 for integers).
  void Configure(float min, float max, float step, int decimals);
  void SetDefault(float value);
  float GetDefault() const { return Default; }
  void SetValue(float value);
  float GetValue() const { return Value; }
  int GetIntValue() const;
  void SetOnChanged(std::function<void(float)> handler)
  {
    OnChanged = std::move(handler);
  }

  void SetBounds(const GuiRect &bounds) override;
  int GetPreferredHeight() const override;
  void Draw(UGuiRenderer &renderer) override;
  bool ConsumesScrollDragAt(int x, int y) const override;

  /// Value as shown in the text box (exposed for tests / tooling).
  std::string FormatValue(float value) const;

private:
  float Snap(float value) const;
  void Apply(float value, bool notify, bool updateText);
  void OnTextEdited();
  void RefreshResetState();

  const GuiTheme *Theme;
  UGuiSlider *Slider{nullptr};
  UGuiTextInput *Input{nullptr};
  UGuiButton *Reset{nullptr};
  float Min{0.f};
  float Max{1.f};
  float Step{0.01f};
  int Decimals{2};
  float Value{0.f};
  float Default{0.f};
  std::function<void(float)> OnChanged;
};

} // namespace cutum

#endif
