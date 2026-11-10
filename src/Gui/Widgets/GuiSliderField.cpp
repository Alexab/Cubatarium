#include "Gui/Widgets/GuiSliderField.h"

#include "Gui/Core/GuiTheme.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiSlider.h"
#include "Gui/Widgets/GuiTextInput.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cutum
{

UGuiSliderField::UGuiSliderField(const GuiTheme *theme) : Theme(theme)
{
  auto slider = std::make_unique<UGuiSlider>(theme);
  Slider = slider.get();
  Slider->SetOnValueChanged([this](float v) { Apply(v, true, true); });
  AddChild(std::move(slider));

  auto input = std::make_unique<UGuiTextInput>(theme);
  Input = input.get();
  Input->SetOnEdited([this]() { OnTextEdited(); });
  AddChild(std::move(input));

  auto reset = std::make_unique<UGuiButton>(theme, "Reset");
  Reset = reset.get();
  Reset->SetOnClick([this]() { Apply(Default, true, true); });
  AddChild(std::move(reset));

  Configure(0.f, 1.f, 0.01f, 2);
}

void UGuiSliderField::Configure(float min, float max, float step, int decimals)
{
  Min = min;
  Max = std::max(min, max);
  Step = step > 0.f ? step : 0.f;
  Decimals = std::clamp(decimals, 0, 4);
  Slider->SetRange(Min, Max);
  Slider->SetStep(Step);
  Apply(Value, false, true);
  Default = Snap(Default);
  RefreshResetState();
}

void UGuiSliderField::SetDefault(float value)
{
  Default = Snap(value);
  RefreshResetState();
}

void UGuiSliderField::SetValue(float value) { Apply(value, false, true); }

int UGuiSliderField::GetIntValue() const
{
  return static_cast<int>(std::lround(Value));
}

float UGuiSliderField::Snap(float value) const
{
  float v = std::clamp(value, Min, Max);
  if (Step > 0.f)
  {
    v = Min + std::round((v - Min) / Step) * Step;
    v = std::clamp(v, Min, Max);
  }
  return v;
}

std::string UGuiSliderField::FormatValue(float value) const
{
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%.*f", Decimals, value);
  return buffer;
}

void UGuiSliderField::Apply(float value, bool notify, bool updateText)
{
  const float before = Value;
  Value = Snap(value);
  Slider->SetValue(Value);
  if (updateText)
  {
    Input->SetText(FormatValue(Value));
  }
  RefreshResetState();
  if (notify && OnChanged && std::fabs(Value - before) > 1e-6f)
  {
    OnChanged(Value);
  }
}

void UGuiSliderField::OnTextEdited()
{
  const std::string &text = Input->GetText();
  if (text.empty() || text == "-" || text == "." || text == "-.")
  {
    return; // still typing
  }
  char *end = nullptr;
  const float parsed = std::strtof(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0')
  {
    return; // not a number yet; keep what the user typed
  }
  // Keep the typed text while focused; it is normalised in Draw() on blur.
  Apply(parsed, true, false);
}

void UGuiSliderField::RefreshResetState()
{
  if (Reset)
  {
    Reset->SetEnabled(std::fabs(Value - Default) > 1e-6f);
  }
}

int UGuiSliderField::GetPreferredHeight() const
{
  return Input ? Input->GetPreferredHeight() : 32;
}

void UGuiSliderField::SetBounds(const GuiRect &bounds)
{
  UGuiWidget::SetBounds(bounds);
  const int gap = Theme ? Theme->Padding : 8;
  const int resetW = std::max(56, bounds.H * 2);
  const int inputW = std::max(64, bounds.H * 2 + gap);
  int sliderW = bounds.W - resetW - inputW - gap * 2;
  if (sliderW < 60)
  {
    sliderW = std::max(20, bounds.W / 3);
  }
  const int remaining = std::max(0, bounds.W - sliderW - gap * 2);
  const int in_w = std::min(inputW, remaining * inputW / std::max(1, inputW + resetW));
  const int rs_w = std::max(0, remaining - in_w);
  Slider->SetBounds({bounds.X, bounds.Y, sliderW, bounds.H});
  Input->SetBounds({bounds.X + sliderW + gap, bounds.Y, in_w, bounds.H});
  Reset->SetBounds({bounds.X + sliderW + gap * 2 + in_w, bounds.Y, rs_w, bounds.H});
}

bool UGuiSliderField::ConsumesScrollDragAt(int x, int y) const
{
  return Slider && Slider->ConsumesScrollDragAt(x, y);
}

void UGuiSliderField::Draw(UGuiRenderer &renderer)
{
  if (!Visible)
  {
    return;
  }
  // Normalise the typed text (clamped, snapped) once the user leaves the box.
  if (!Input->IsFocused())
  {
    const std::string formatted = FormatValue(Value);
    if (Input->GetText() != formatted)
    {
      Input->SetText(formatted);
    }
  }
  UGuiWidget::Draw(renderer);
}

} // namespace cutum
