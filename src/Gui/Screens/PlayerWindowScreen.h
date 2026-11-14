#pragma once
#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Layout/PlayerWindowLayout.h"
#include <array>
namespace cutum
{
class UGameSession;
class UCharacterSheetScreen;
class UCreativePaletteScreen;
class IUGuiIconSource;
class UGuiWindow;
class UGuiTabBar;
class UGuiSlot;
class UPlayerSectionHost;
class UPlayerWindowScreen : public UGuiScreenBase
{
public:
  UPlayerWindowScreen(UGameSession *session, UCharacterSheetScreen *character,
                      UCreativePaletteScreen *catalog, IUGuiIconSource *icons);
  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  void OnViewportChanged(int width, int height) override;
  void OnMetricsChanged(const GuiMetrics &metrics) override;
  void SelectSection(int section);
  bool PickHotbarSlot(int x, int y, SlotAddress &out) const;
  GuiRect GetTabBounds(int section) const;
  GuiRect GetHotbarBounds(size_t index) const;
  const PlayerWindowLayout &GetLayout() const { return Layout; }
  int GetSection() const { return Section; }

private:
  void Relayout();
  UGameSession *Session;
  UCharacterSheetScreen *Character;
  UCreativePaletteScreen *Catalog;
  IUGuiIconSource *Icons;
  UGuiWindow *Window{nullptr};
  UGuiTabBar *Tabs{nullptr};
  UPlayerSectionHost *Host{nullptr};
  std::array<UGuiSlot *, 10> Slots{};
  PlayerWindowLayout Layout;
  int Section{0};
};
} // namespace cutum
