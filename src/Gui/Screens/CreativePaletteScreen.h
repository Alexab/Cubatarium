#ifndef CREATIVE_PALETTE_SCREEN_H
#define CREATIVE_PALETTE_SCREEN_H

#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include <memory>
#include <string>

namespace cutum
{

class IContentCatalog;
class UGameSession;
class IGuiIconSource;
class UGuiTabBar;
class UGuiScrollView;
class UGuiPanel;
class UGuiSlot;
class UGuiLabel;
class UGuiRenderer;
struct GuiTheme;

class UCreativePaletteScreen : public UGuiScreenBase
{
public:
  UCreativePaletteScreen(IContentCatalog *catalog, UGameSession *session,
                         IGuiIconSource *icons);

  bool PickSlot(int x, int y, SlotAddress &out) const;

  void OnViewportChanged(int width, int height) override;

  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void Toggle();
  void InvalidateGrid() { Built = false; }
  void SetPointerPosition(int x, int y);
  void SetPointerPressed(bool pressed);

private:
  void UpdateTooltip();
  void RebuildGrid();
  void RelayoutPanel();
  void LayoutGridInScroll();

  IContentCatalog *Catalog{nullptr};
  UGameSession *Session{nullptr};
  IGuiIconSource *Icons{nullptr};
  std::vector<std::string> GridEntryIds;
  std::vector<std::string> GridEntryLabels;
  std::vector<std::string> GridSpawnHints;
  UGuiPanel *Panel{nullptr};
  UGuiTabBar *MainTabs{nullptr};
  UGuiTabBar *SubTabs{nullptr};
  UGuiScrollView *Scroll{nullptr};
  UGuiLabel *TooltipLabel{nullptr};
  UGuiRenderer *Renderer{nullptr};
  std::vector<UGuiSlot *> GridSlots;
  int PointerX{-1};
  int PointerY{-1};
  bool PointerPressed{false};
  double HoldTimer{0.0};
  int HoldSlotIndex{-1};
  static constexpr double kHoldTooltipSeconds = 0.45;
  std::string SelectedEntryId;
  ContentKind Kind{ContentKind::Block};
  std::string ActiveTypeId;
  const GuiTheme *Theme{nullptr};
  bool Visible{false};
  bool Built{false};
};

} // namespace cutum

#endif
