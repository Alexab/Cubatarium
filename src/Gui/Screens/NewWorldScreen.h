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
class UGuiPanel;

class UNewWorldScreen : public UGuiScreenBase
{
public:
  explicit UNewWorldScreen(IGuiMenuHost *host);

  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;

private:
  void Relayout();
  void OnCreate();
  int MeasureWorldPageHeight(const GuiRect &area) const;
  void LayoutWorldPage(const GuiRect &area) const;

  IGuiMenuHost *host_{nullptr};
  UGuiWindow *Window{nullptr};
  UGuiDialogFrame *dialogFrame_{nullptr};
  UGuiPanel *worldPage_{nullptr};
  std::unique_ptr<UWorldGenSettingsForm> worldForm_;
};

} // namespace cutum
