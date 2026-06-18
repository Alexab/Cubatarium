#pragma once

#include "App/Settings/UiSettings.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiTypes.h"
#include "Gui/Layout/GuiLayout.h"
#include <memory>
#include <vector>

namespace cutum
{

class IGuiMenuHost;
class UGuiPanel;
class UGuiWindow;
class UGuiDialogFrame;
class UWorldGenSettingsForm;
class UResourcePackPickerForm;
class UGuiTextInput;
class UGuiCheckbox;
class UGuiLabel;
class UGuiButton;

class USettingsScreen : public UGuiScreenBase
{
public:
  explicit USettingsScreen(IGuiMenuHost *host);
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

  IGuiMenuHost *Host{nullptr};
  UGuiWindow *Window{nullptr};
  UGuiDialogFrame *DialogFrame{nullptr};
  UGuiPanel *AppPanel{nullptr};
  UGuiPanel *WorldPanel{nullptr};
  std::unique_ptr<UWorldGenSettingsForm> WorldForm;

  UGuiLabel *DefaultUserLabel{nullptr};
  UGuiLabel *DefaultWorldLabel{nullptr};
  UGuiLabel *RenderDistLabel{nullptr};
  UGuiLabel *ConsoleKeyLabel{nullptr};
  UGuiLabel *PaletteKeyLabel{nullptr};
  UGuiLabel *HotbarCountLabel{nullptr};
  UGuiLabel *HotbarCountValueLabel{nullptr};
  UGuiLabel *ControlSchemeLabel{nullptr};
  UGuiButton *ControlSchemeButton{nullptr};
  ControlScheme SelectedControlScheme{ControlScheme::Classic};

  UGuiTextInput *DefaultUserInput{nullptr};
  UGuiTextInput *DefaultWorldInput{nullptr};
  UGuiTextInput *RenderDistInput{nullptr};
  UGuiCheckbox *StreamingBox{nullptr};
  UGuiCheckbox *StepUpBox{nullptr};
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
  UGuiLabel *PackSectionLabel{nullptr};
  UGuiLabel *PackNoteLabel{nullptr};
  std::unique_ptr<UResourcePackPickerForm> PackForm;
  int HotbarCount{1};
};

} // namespace cutum
