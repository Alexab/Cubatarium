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

class UNewWorldScreen : public UGuiScreenBase
{
public:
  explicit UNewWorldScreen(IGuiMenuHost *host);
  ~UNewWorldScreen();

  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;

private:
  void Relayout();
  void OnCreate();
  int MeasureWorldPageHeight(const GuiRect &area) const;
  void LayoutWorldPage(const GuiRect &area) const;

  IGuiMenuHost *Host{nullptr};
  UGuiWindow *Window{nullptr};
  UGuiDialogFrame *DialogFrame{nullptr};
  UGuiPanel *WorldPage{nullptr};
  std::unique_ptr<UWorldGenSettingsForm> WorldForm;
  UGuiLabel *PackSectionLabel{nullptr};
  std::unique_ptr<UResourcePackPickerForm> PackForm;
};

} // namespace cutum
