#ifndef GUI_PROGRESS_BAR_H
#define GUI_PROGRESS_BAR_H

#include "Gui/Widgets/GuiWidget.h"
#include <string>

namespace cutum
{

struct GuiTheme;

class UGuiProgressBar : public UGuiWidget
{
public:
  explicit UGuiProgressBar(const GuiTheme *theme);

  void SetValue(float value);
  float GetValue() const { return Value; }
  void SetIndeterminate(bool indeterminate);
  bool IsIndeterminate() const { return Indeterminate; }
  void SetShowPercent(bool show) { ShowPercent = show; }
  void SetLabel(const std::string &text) { Label = text; }
  const std::string &GetLabel() const { return Label; }

  int GetPreferredHeight() const override;
  int GetPreferredWidth() const override;
  void Update(double dt) override;
  void Draw(UGuiRenderer &renderer) override;

private:
  const GuiTheme *Theme;
  std::string Label;
  float Value{0.f};
  bool Indeterminate{false};
  bool ShowPercent{false};
  float IndeterminateOffset{0.f};
};

} // namespace cutum

#endif
