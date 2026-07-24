#pragma once

#include "Gui/Core/GuiTypes.h"
#include "World/View/WorldViewSettings.h"
#include <functional>
#include <memory>

namespace cutum
{

struct GuiTheme;
class UGuiPanel;
class UGuiLabel;
class UGuiListView;
class UGuiTextInput;

class UWorldViewSettingsForm
{
public:
  explicit UWorldViewSettingsForm(const GuiTheme *theme);

  void SetSettings(const WorldViewSettings &settings);
  WorldViewSettings ReadSettings() const;
  void SetOnLayoutChanged(std::function<void()> handler);

  void BuildInto(UGuiPanel &panel);
  int MeasureHeight(const GuiRect &area) const;
  void Layout(const GuiRect &area) const;

private:
  void UpdateFieldVisibility();

  const GuiTheme *Theme;
  WorldViewSettings FormSettings;
  bool Built{false};
  std::function<void()> OnLayoutChanged;

  UGuiLabel *SectionLabel{nullptr};
  UGuiLabel *ProjectionLabel{nullptr};
  UGuiListView *ProjectionList{nullptr};
  UGuiLabel *OrthoSizeLabel{nullptr};
  UGuiTextInput *OrthoSizeInput{nullptr};
};

} // namespace cutum
