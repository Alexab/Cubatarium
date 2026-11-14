#ifndef CREATIVE_PALETTE_SCREEN_H
#define CREATIVE_PALETTE_SCREEN_H

#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Interfaces/IUContentCatalog.h"
#include "Gui/Interfaces/IUInventoryViewModel.h"
#include <memory>
#include <string>
#include <vector>

namespace cutum
{

class IUContentCatalog;
class UGameSession;
class IUGuiIconSource;
class UContentPreviewRenderer;
class UContentPreviewDock;
class UGuiTabBar;
class UGuiScrollView;
class UGuiPanel;
class UGuiSlot;
class UGuiLabel;
class UGuiButton;
class UGuiRenderer;
struct GuiTheme;

class UCreativePaletteScreen : public UGuiScreenBase
{
public:
  UCreativePaletteScreen(IUContentCatalog *catalog, UGameSession *session,
                         IUGuiIconSource *icons,
                         UContentPreviewRenderer *previewRenderer);
  ~UCreativePaletteScreen();

  bool PickSlot(int x, int y, SlotAddress &out) const;
  /// Hotbar strip only (priority drop target while palette is open).
  bool PickHotbarStrip(int x, int y, SlotAddress &out) const;
  /// Creative grid cell only (trash / identity), not hotbar.
  bool PickGridSlot(int x, int y, SlotAddress &out) const;

  void OnViewportChanged(int width, int height) override;

  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void Toggle();
  void OpenWithMainTab(int tab);
  /// Creative may show the full catalog. Survival is backpack-only.
  void SetCatalogAllowed(bool allowed);
  int GetActiveMainTab() const;
  size_t GetGridEntryCount() const { return GridEntryIds.size(); }
  bool IsGridComplete() const
  {
    return Built && GridBuiltCount == GridPendingViews.size();
  }
  void InvalidateGrid() { Built = false; }
  void SetPointerPosition(int x, int y);
  void SetPointerPressed(bool pressed);
  void RenderPreview();

private:
  void ApplyMainTab(int tab);
  void UpdateTooltip();
  void RebuildGrid();
  void AppendGridSlotsChunk();
  void FinishGridBuild();
  void RefreshGridIconTextures();
  void RelayoutPanel();
  void LayoutGridInScroll();
  void SyncPreviewDock();
  void EnsureHotbarStrip();
  void LayoutHotbarStrip();
  void SyncHotbarStrip();
  void SetListingBackpack(bool backpack);
  void RefreshListingPolicy();
  void SyncTypeTabs();
  InventoryMode CurrentListingMode() const;

  enum class PaletteListing
  {
    Catalog,
    Backpack
  };

  IUContentCatalog *Catalog{nullptr};
  UGameSession *Session{nullptr};
  IUGuiIconSource *Icons{nullptr};
  UContentPreviewRenderer *PreviewRenderer{nullptr};
  std::unique_ptr<UContentPreviewDock> PreviewDock;
  UGuiPanel *RootShell{nullptr};
  std::vector<std::string> GridEntryIds;
  std::vector<std::string> GridEntryLabels;
  std::vector<std::string> GridSpawnHints;
  UGuiPanel *Panel{nullptr};
  UGuiTabBar *PlayerTabs{nullptr};
  UGuiTabBar *MainTabs{nullptr};
  UGuiTabBar *SubTabs{nullptr};
  UGuiButton *CatalogButton{nullptr};
  UGuiButton *BackpackButton{nullptr};
  UGuiScrollView *Scroll{nullptr};
  UGuiLabel *TooltipLabel{nullptr};
  UGuiLabel *UsageHintLabel{nullptr};
  UGuiLabel *HotbarStripLabel{nullptr};
  UGuiPanel *HotbarStripDivider{nullptr};
  UGuiRenderer *Renderer{nullptr};
  std::vector<UGuiSlot *> GridSlots;
  /// UX tail: the grid materializes in chunks per frame - opening the
  /// Blocks tab (1754 entries) used to construct every widget in one tick.
  std::vector<InventoryEntryView> GridPendingViews;
  size_t GridBuiltCount{0};
  std::vector<UGuiSlot *> HotbarStripSlots;
  int PointerX{-1};
  int PointerY{-1};
  bool PointerPressed{false};
  double HoldTimer{0.0};
  int HoldSlotIndex{-1};
  static constexpr double kHoldTooltipSeconds = 0.45;
  std::string SelectedEntryId;
  ContentKind Kind{ContentKind::Block};
  std::string ActiveTypeId;
  PaletteListing Listing{PaletteListing::Catalog};
  bool CatalogAllowed{true};
  const GuiTheme *Theme{nullptr};
  bool Visible{false};
  bool Built{false};
  bool HotbarStripBuilt{false};
  // Per-frame type-tab cache (see SyncTypeTabs): emptiness of the catalog
  // groups is only recomputed when the inventory signature changes.
  bool TypeTabsCacheValid{false};
  bool TypeTabsUiDirty{false};
  size_t TypeTabSignature{0};
  ContentKind TypeTabKind{ContentKind::Block};
  InventoryMode TypeTabMode{InventoryMode::Creative};
  std::vector<std::string> TypeTabTypes;
  std::vector<std::string> TypeTabLabels;
  std::vector<bool> TypeTabEnabled;
};

} // namespace cutum

#endif
