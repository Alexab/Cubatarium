#pragma once

#include "Gui/Core/GuiScreenBase.h"

namespace cutum
{

class IUGuiMenuHost;
class UGuiWindow;
class UGuiDialogFrame;
class UGuiListView;
class UGuiLabel;
class UGuiButton;
class UGuiPanel;

class ULoadWorldScreen : public UGuiScreenBase
{
public:
  explicit ULoadWorldScreen(IUGuiMenuHost *host);

  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;

private:
  void Relayout();
  void OnLoad();

  IUGuiMenuHost *Host{nullptr};
  UGuiWindow *Window{nullptr};
  UGuiDialogFrame *DialogFrame{nullptr};
  UGuiListView *List{nullptr};
  UGuiLabel *EmptyLabel{nullptr};
  UGuiLabel *PackSubtitle{nullptr};
  UGuiPanel *BodyPanel{nullptr};
  UGuiButton *LoadBtn{nullptr};
};

} // namespace cutum
