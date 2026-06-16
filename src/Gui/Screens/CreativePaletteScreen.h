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

private:
  void RebuildGrid();
  void RelayoutPanel();
  void LayoutGridInScroll();

  IContentCatalog *Catalog{nullptr};
  UGameSession *Session{nullptr};
  IGuiIconSource *Icons{nullptr};
  std::vector<std::string> GridEntryIds;
  UGuiPanel *Panel{nullptr};
  UGuiTabBar *MainTabs{nullptr};
  UGuiTabBar *SubTabs{nullptr};
  UGuiScrollView *Scroll{nullptr};
  std::vector<UGuiSlot *> GridSlots;
  std::string SelectedEntryId;
  ContentKind Kind{ContentKind::Block};
  std::string ActiveTypeId;
  const GuiTheme *Theme{nullptr};
  bool Visible{false};
  bool Built{false};
};

} // namespace cutum

#endif
