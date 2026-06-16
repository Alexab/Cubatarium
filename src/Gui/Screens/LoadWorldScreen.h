#pragma once

#include "Gui/Core/GuiScreenBase.h"

namespace cutum
{

class IGuiMenuHost;
class UGuiWindow;
class UGuiDialogFrame;
class UGuiListView;
class UGuiLabel;
class UGuiButton;

class ULoadWorldScreen : public UGuiScreenBase
{
public:
  explicit ULoadWorldScreen(IGuiMenuHost *host);

  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;

private:
  void Relayout();
  void OnLoad();

  IGuiMenuHost *Host{nullptr};
  UGuiWindow *Window{nullptr};
  UGuiDialogFrame *DialogFrame{nullptr};
  UGuiListView *List{nullptr};
  UGuiLabel *EmptyLabel{nullptr};
  UGuiButton *LoadBtn{nullptr};
};

} // namespace cutum
