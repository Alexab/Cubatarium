#pragma once

#include "App/Settings/UiSettings.h"
#include "App/Settings/RenderSettings.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiTypes.h"
#include "Gui/Layout/GuiLayout.h"
#include <memory>
#include <vector>

namespace cutum
{

class IUGuiMenuHost;
class UGuiPanel;
class UGuiWindow;
class UGuiDialogFrame;
class UWorldGenSettingsForm;
class UResourcePackPickerForm;
class UGuiTextInput;
class UGuiCheckbox;
class UGuiLabel;
class UGuiButton;
class UGuiSlider;

class USettingsScreen : public UGuiScreenBase
{
public:
  explicit USettingsScreen(IUGuiMenuHost *host);
  ~USettingsScreen();

  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;

private:
  void Relayout();
  void ShowTab(int tab);
  void OnSave();
  int MeasureAppPageHeight(const GuiRect &area) const;
  void LayoutAppPage(const GuiRect &area) const;
  void LayoutHotbarCountControls(const GuiGridSpec &spec) const;
  std::vector<GuiGridItem> BuildAppGridItems(const GuiGridSpec &spec) const;
  int MeasureWorldPageHeight(const GuiRect &area) const;
  void LayoutWorldPage(const GuiRect &area) const;
  int MeasurePacksPageHeight(const GuiRect &area) const;
  void LayoutPacksPage(const GuiRect &area) const;

  IUGuiMenuHost *Host{nullptr};
  UGuiWindow *Window{nullptr};
  UGuiDialogFrame *DialogFrame{nullptr};
  UGuiPanel *AppPanel{nullptr};
  UGuiPanel *WorldPanel{nullptr};
  UGuiPanel *PacksPanel{nullptr};
  std::unique_ptr<UWorldGenSettingsForm> WorldForm;
  std::unique_ptr<UResourcePackPickerForm> PackForm;

  UGuiLabel *DefaultUserLabel{nullptr};
  UGuiLabel *DefaultWorldLabel{nullptr};
  UGuiLabel *RenderDistLabel{nullptr};
  UGuiLabel *ConsoleKeyLabel{nullptr};
  UGuiLabel *PaletteKeyLabel{nullptr};
  UGuiLabel *HotbarCountLabel{nullptr};
  UGuiLabel *HotbarCountValueLabel{nullptr};
  UGuiLabel *UiScaleLabel{nullptr};
  UGuiLabel *UiScaleValueLabel{nullptr};
  UGuiSlider *UiScaleSlider{nullptr};
  UGuiLabel *ControlSchemeLabel{nullptr};
  UGuiButton *ControlSchemeButton{nullptr};
  ControlScheme SelectedControlScheme{ControlScheme::Classic};
  UGuiLabel *GraphicsQualityLabel{nullptr};
  UGuiButton *GraphicsQualityButton{nullptr};
  PerformancePreset SelectedGraphicsQuality{PerformancePreset::Balanced};
  UGuiLabel *LightingModeLabel{nullptr};
  UGuiButton *LightingModeButton{nullptr};
  LightingMode SelectedLightingMode{LightingMode::Full};
  UGuiCheckbox *AsyncMeshingBox{nullptr};
  UGuiCheckbox *AsyncRelightBox{nullptr};
  UGuiCheckbox *AndroidGpuBox{nullptr};
  bool SelectedAndroidGpuEnabled{true};

  UGuiTextInput *DefaultUserInput{nullptr};
  UGuiTextInput *DefaultWorldInput{nullptr};
  UGuiTextInput *RenderDistInput{nullptr};
  UGuiCheckbox *StreamingBox{nullptr};
  UGuiCheckbox *StepUpBox{nullptr};
  UGuiCheckbox *FoliageClimbBox{nullptr};
  UGuiCheckbox *GreedyBox{nullptr};
  UGuiCheckbox *FaceQuadsBox{nullptr};
  UGuiCheckbox *FrustumBox{nullptr};
  UGuiCheckbox *BatchCacheBox{nullptr};
  UGuiCheckbox *LegacyHudBox{nullptr};
  UGuiCheckbox *ShowPerformanceBox{nullptr};
  UGuiTextInput *ConsoleKeyInput{nullptr};
  UGuiTextInput *PaletteKeyInput{nullptr};
  UGuiButton *HotbarMinusButton{nullptr};
  UGuiButton *HotbarPlusButton{nullptr};
  int HotbarCount{1};
  float UiScaleUser{1.f};
};

} // namespace cutum
