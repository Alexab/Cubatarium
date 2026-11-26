#ifndef CREATIVE_PALETTE_SCREEN_H
#define CREATIVE_PALETTE_SCREEN_H

#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiTypes.h"
#include "Gui/Interfaces/IUContentCatalog.h"
#include "Gui/Interfaces/IUInventoryViewModel.h"
#include <memory>
#include <string>
#include <utility>
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

struct PaletteGridStorage
{
  size_t rows{}, widgets{}, row_bytes{}, row_text_bytes{}, child_bytes{}, widget_bytes{};
  size_t widget_text_bytes{}, callbacks{}, callback_bytes{}, requested{};
  ContentKind kind{ContentKind::Block};
  bool current{}, complete{}, supported{};
};

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
  void SetHosted(bool hosted)
  {
    Hosted = hosted;
    RelayoutPanel();
  }
  void Toggle();
  void OpenWithMainTab(int tab);
  /// Creative may show the full catalog. Survival is backpack-only.
  void SetCatalogAllowed(bool allowed);
  int GetActiveMainTab() const;
  size_t GetGridEntryCount() const;
  bool IsGridComplete() const
  {
    return Built && GetGridEntryCount() == GridViews.size();
  }
  size_t GetGridIconCount() const;
  std::string GetSectionStateKey() const;
  GuiRect GetPreviewBounds() const;
  void InvalidateGrid() { Built = false; }
  void SetPointerPosition(int x, int y);
  void SetPointerPressed(bool pressed);
  void RenderPreview();
  PaletteGridStorage InspectGridStorage(const IUContentCatalog &catalog,
                                        const UGameSession &session) const noexcept;

private:
  friend struct PaletteGridSourceProbe;
  void ApplyMainTab(int tab);
  void UpdateTooltip();
  void RebuildGrid();
  void AppendGridSlotsChunk();
  class GridSlot;
  struct GridClickAction;
  struct GridDragAction;
  using GridWidgets = std::vector<std::unique_ptr<UGuiWidget>>;
  const GridWidgets &GridChildren() const;
  GridSlot &GridSlotAt(size_t index) const;
  GridWidgets BuildGridChunk(const std::vector<InventoryEntryView> &views,
                             size_t start, ContentKind kind);
  void ReleaseMaterializedLabels(size_t start) noexcept;
  void RegisterGridIcons();
  std::pair<size_t, size_t> VisibleGridRange() const;
  void ClickGridSlot(size_t index);
  void DragGridSlot(size_t index);
  void RefreshGridIconTextures();
  size_t WarmVisibleGridIcons();
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
  // The scroll content owns the published prefix of widgets for these rows.
  std::vector<InventoryEntryView> GridViews;
  ContentKind GridKind{ContentKind::Block};
  bool GridPreviewPending{false};
  size_t GridRegisteredIcons{0};
  size_t GridIconCursor{0};
  size_t GridCacheCursor{0};
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
  bool Hosted{false};
  bool HotbarStripBuilt{false};
  // Per-frame type-tab cache (see SyncTypeTabs): emptiness of the catalog
  // groups is only recomputed when the inventory signature changes.
  bool TypeTabsCacheValid{false};
  size_t TypeTabSignature{0};
  ContentKind TypeTabKind{ContentKind::Block};
  InventoryMode TypeTabMode{InventoryMode::Creative};
  std::vector<std::string> TypeTabTypes;
  std::vector<std::string> TypeTabLabels;
  std::vector<bool> TypeTabEnabled;
};

} // namespace cutum

#endif
