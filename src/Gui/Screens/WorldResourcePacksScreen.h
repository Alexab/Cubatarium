#pragma once

#include "Gui/Core/GuiScreenBase.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include <functional>
#include <memory>

namespace cutum
{

class IGuiMenuHost;
class UResourcePackPickerForm;
class UGuiPanel;
class UGuiWindow;
class UGuiDialogFrame;
class UGuiScrollView;
class UGuiLabel;

class UWorldResourcePacksScreen : public UGuiScreenBase
{
public:
  UWorldResourcePacksScreen(IGuiMenuHost *host,
                            std::function<void()> onClose);

  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;
  void Update(double dt) override;
  bool BlocksGameInput() const override { return true; }

private:
  void Relayout();
  void LayoutBody(UGuiScrollView &scroll) const;
  int MeasureBodyHeight(int width) const;
  void OnApply();

  IGuiMenuHost *Host{nullptr};
  std::function<void()> OnClose;
  UGuiWindow *Window{nullptr};
  UGuiDialogFrame *DialogFrame{nullptr};
  UGuiScrollView *BodyScroll{nullptr};
  UGuiPanel *BodyPanel{nullptr};
  UGuiLabel *WarningLabel{nullptr};
  std::unique_ptr<UResourcePackPickerForm> PackForm;
};

} // namespace cutum
