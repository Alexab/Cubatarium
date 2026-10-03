#ifndef ANVIL_SCREEN_H
#define ANVIL_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"

#include <string>

namespace cutum
{

class UWorld;
class IUGuiIconSource;
class UGuiPanel;
class UGuiLabel;
class UGuiButton;
class UGuiSlot;
struct GuiTheme;

/// Repair UI for the active hotbar item (uses item repair materials).
class UAnvilScreen : public UGuiScreenBase
{
public:
  UAnvilScreen(UWorld *world, IUGuiIconSource *icons);
  ~UAnvilScreen() override;

  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void Toggle();
  void SetWorld(UWorld *world) { World = world; }

  void OnViewportChanged(int width, int height) override;

private:
  void Refresh();
  void Relayout();
  void TryRepair();

  UWorld *World{nullptr};
  IUGuiIconSource *Icons{nullptr};

  UGuiPanel *Panel{nullptr};
  UGuiLabel *Title{nullptr};
  UGuiLabel *ItemLabel{nullptr};
  UGuiLabel *Status{nullptr};
  UGuiSlot *ItemSlot{nullptr};
  UGuiButton *RepairBtn{nullptr};
  const GuiTheme *Theme{nullptr};
  bool Visible{false};
  std::string StatusText;
};

} // namespace cutum

#endif
