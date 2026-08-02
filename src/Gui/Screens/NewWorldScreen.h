#pragma once

#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiTypes.h"
#include "Game/WorldGameMode.h"
#include <memory>

namespace cutum
{

class IUGuiMenuHost;
class UGuiWindow;
class UGuiDialogFrame;
class UWorldGenSettingsForm;
class UWorldViewSettingsForm;
class UResourcePackPickerForm;
class UGuiPanel;
class UGuiLabel;
class UGuiListView;
class UGuiScrollView;

class UNewWorldScreen : public UGuiScreenBase
{
public:
  explicit UNewWorldScreen(IUGuiMenuHost *host);
  ~UNewWorldScreen();

  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  void OnViewportChanged(int width, int height) override;

private:
  void Relayout();
  void RequestBodyRelayout();
  void OnCreate();
  int MeasureWorldPageContentHeight(int width) const;
  void LayoutWorldPageInScroll(UGuiScrollView &scroll) const;
  void LayoutWorldPage(const GuiRect &area) const;
  WorldGameMode ReadSelectedGameMode() const;

  IUGuiMenuHost *Host{nullptr};
  UGuiWindow *Window{nullptr};
  UGuiDialogFrame *DialogFrame{nullptr};
  UGuiScrollView *BodyScroll{nullptr};
  UGuiPanel *WorldPage{nullptr};
  std::unique_ptr<UWorldGenSettingsForm> WorldForm;
  UGuiLabel *ViewSectionLabel{nullptr};
  std::unique_ptr<UWorldViewSettingsForm> ViewForm;
  UGuiLabel *GameModeSectionLabel{nullptr};
  UGuiListView *GameModeList{nullptr};
  UGuiLabel *PackSectionLabel{nullptr};
  std::unique_ptr<UResourcePackPickerForm> PackForm;
  bool NeedsBodyRelayout{false};
};

} // namespace cutum
