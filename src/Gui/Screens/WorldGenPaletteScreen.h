#ifndef WORLD_GEN_PALETTE_SCREEN_H
#define WORLD_GEN_PALETTE_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Interfaces/IUContentCatalog.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "WorldGen/Core/WorldGenSets.h"
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UWorld;
class IUGuiIconSource;
class UContentPreviewRenderer;
class UContentPreviewDock;
class UGuiTabBar;
class UGuiPanel;
class UGuiLabel;
class UGuiButton;
class UGuiScrollView;
class UGuiRenderer;
struct GuiTheme;

struct WorldGenPaletteGridStorage
{
  std::size_t contentBytes{0};
  std::size_t pickerBytes{0};
  std::size_t sectionTabBytes{0};
  std::size_t pickerTitleBytes{0};
  bool supported{false};
};

struct WorldGenPaletteStorage
{
  WorldGenPaletteGridStorage grid;
  std::size_t screenBytes{}, widgetBytes{}, childrenBytes{}, tabBytes{}, textBytes{};
  std::size_t callbackBytes{}, previewBytes{}, totalBytes{}, widgets{};
  bool present{}, visible{}, bindingsMatch{}, previewTexturePresent{}, supported{};
};

class UWorldGenPaletteScreen : public UGuiScreenBase
{
public:
  UWorldGenPaletteScreen(UWorld *world, IUContentCatalog *catalog,
                         IUGuiIconSource *icons,
                         UContentPreviewRenderer *previewRenderer);
  ~UWorldGenPaletteScreen();

  void OnViewportChanged(int width, int height) override;
  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void Toggle();
  void SetPointerPosition(int x, int y);
  void SetPointerPressed(bool pressed);
  void RenderPreview();
  WorldGenPaletteGridStorage InspectGridStorage() const noexcept;
  WorldGenPaletteStorage InspectOwnedStorage(
      const UGuiContext &context, const UWorld *world, const IUContentCatalog *catalog,
      const IUGuiIconSource *icons, const UContentPreviewRenderer *preview) const noexcept;

private:
  friend struct WorldGenPaletteStorageProbe;
  class GridSlot;
  struct SlotAction;
  struct TabAction;
  struct GridLayoutAction;
  template<void (UWorldGenPaletteScreen::*Action)()> struct ButtonAction;
  enum class PickerMode
  {
    None,
    Object,
    TerrainSlot,
  };

  void RelayoutPanel();
  void RebuildMainContent();
  void PublishPicker();
  void ClosePicker();
  void OpenObjectPicker();
  void OpenTerrainPicker();
  void ToggleOre();
  void OnPickerPick(const std::string &id);
  void SaveSets();
  void OnResetSection();
  std::unordered_map<std::string, WorldGenObjectSection> *ObjectPool(int tab) const;

  void UpdateTooltip();
  void LayoutIconGrid(UGuiScrollView &scroll);
  std::vector<CatalogEntry> CollectCatalogEntries(ContentKind kind) const;
  std::string DisplayNameForId(ContentKind kind, const std::string &id,
                               const std::vector<CatalogEntry> &entries) const;
  GLuint IconForEntry(ContentKind kind, const std::string &id) const;
  void ActivateSlot(const GridSlot &slot);
  void SelectEntry(ContentKind kind, const std::string &id,
                   const std::string &terrainSlot, const std::string &oreSlot);
  std::string StoredTerrainBlock(const std::string &slotName) const;
  std::string EffectiveTerrainBlock(const std::string &slotName) const;
  void SyncPreviewDock();
  void ApplySlotSelection();

  UWorld *World{nullptr};
  IUContentCatalog *Catalog{nullptr};
  IUGuiIconSource *Icons{nullptr};
  UContentPreviewRenderer *PreviewRenderer{nullptr};
  std::unique_ptr<UContentPreviewDock> PreviewDock;
  UGuiPanel *RootShell{nullptr};
  UGuiPanel *Panel{nullptr};
  UGuiTabBar *MainTabs{nullptr};
  UGuiTabBar *ObjectPoolTabs{nullptr};
  UGuiTabBar *SectionTabs{nullptr};
  UGuiLabel *BannerLabel{nullptr};
  UGuiScrollView *ContentScroll{nullptr};
  UGuiButton *AddButton{nullptr};
  UGuiButton *ResetButton{nullptr};
  UGuiPanel *PickerPanel{nullptr};
  UGuiScrollView *PickerScroll{nullptr};
  UGuiButton *PickerCloseButton{nullptr};
  UGuiLabel *PickerTitleLabel{nullptr};
  UGuiLabel *TooltipLabel{nullptr};
  UGuiRenderer *Renderer{nullptr};
  const GuiTheme *Theme{nullptr};
  int PointerX{-1};
  int PointerY{-1};
  bool PointerPressed{false};
  int ActiveTab{0};
  int ObjectPoolTab{0};
  int SectionTab{0};
  int RequestedTab{0};
  int RequestedPoolTab{0};
  int RequestedSectionTab{0};
  PickerMode Picker{PickerMode::None};
  PickerMode RequestedPicker{PickerMode::None};
  std::string PickerTerrainSlot;
  std::string RequestedTerrainSlot;
  std::string SelectedEntryId;
  ContentKind SelectedKind{ContentKind::Object};
  std::string SelectedTerrainSlot;
  std::string SelectedOreSlot;
  bool Visible{false};
  bool Built{false};
  bool ContentDirty{true};
  bool PickerDirty{false};
  bool PreviewDirty{false};
};

} // namespace cutum

#endif
