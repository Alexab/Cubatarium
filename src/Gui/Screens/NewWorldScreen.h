#pragma once

#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiTypes.h"
#include <memory>

namespace cutum
{

class IGuiMenuHost;
class UGuiWindow;
class UGuiDialogFrame;
class UWorldGenSettingsForm;
class UResourcePackPickerForm;
class UGuiPanel;
class UGuiLabel;
class UGuiScrollView;

class UNewWorldScreen : public UGuiScreenBase
{
public:
  explicit UNewWorldScreen(IGuiMenuHost *host);
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

  IGuiMenuHost *Host{nullptr};
  UGuiWindow *Window{nullptr};
  UGuiDialogFrame *DialogFrame{nullptr};
  UGuiScrollView *BodyScroll{nullptr};
  UGuiPanel *WorldPage{nullptr};
  std::unique_ptr<UWorldGenSettingsForm> WorldForm;
  UGuiLabel *PackSectionLabel{nullptr};
  std::unique_ptr<UResourcePackPickerForm> PackForm;
  bool NeedsBodyRelayout{false};
};

} // namespace cutum
