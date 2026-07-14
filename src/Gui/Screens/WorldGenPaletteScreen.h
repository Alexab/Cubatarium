#ifndef WORLD_GEN_PALETTE_SCREEN_H
#define WORLD_GEN_PALETTE_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Interfaces/IUContentCatalog.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "WorldGen/Core/WorldGenSets.h"
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
class UGuiSlot;
class UGuiRenderer;
struct GuiTheme;

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

private:
  enum class PickerMode
  {
    None,
    Object,
    TerrainSlot,
  };

  void RelayoutPanel();
  void RebuildMainContent();
  void RebuildPicker();
  void ClosePicker();
  void OpenObjectPicker();
  void OpenTerrainPicker(const std::string &slotName);
  void OnPickerPick(const std::string &id);
  void SaveSets();
  void OnResetSection();
  void EnsureActiveSection();
  std::vector<std::string> SectionKeysForPool() const;
  std::unordered_map<std::string, WorldGenObjectSection> *ActiveObjectPool();

  void UpdateTooltip();
  void LayoutContentIconGrid();
  void LayoutPickerIconGrid();
  std::vector<CatalogEntry> CollectCatalogEntries(ContentKind kind) const;
  std::string DisplayNameForId(ContentKind kind, const std::string &id) const;
  GLuint IconForEntry(ContentKind kind, const std::string &id) const;
  void AddPickerSlot(const CatalogEntry &entry, ContentKind kind);
  void SelectEntry(ContentKind kind, const std::string &id,
                   const std::string &displayName,
                   const std::string &terrainSlot = "",
                   const std::string &oreSlot = "");
  std::string StoredTerrainBlock(const std::string &slotName) const;
  std::string EffectiveTerrainBlock(const std::string &slotName) const;
  std::string TerrainSlotTooltip(const std::string &slotName) const;
  std::string OreSlotTooltip(const std::string &oreSlot, bool enabled) const;
  void SyncPreviewDock();
  void ApplySlotSelection();
  std::string LabelAt(size_t index, bool picker) const;

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
  std::vector<UGuiSlot *> ContentSlots;
  std::vector<std::string> ContentSlotLabels;
  std::vector<std::string> ContentSlotHints;
  std::vector<ContentKind> ContentSlotKinds;
  std::vector<std::string> ContentSlotIds;
  std::vector<std::string> ContentSlotTerrainSlots;
  std::vector<UGuiSlot *> PickerSlots;
  std::vector<std::string> PickerSlotLabels;
  int PointerX{-1};
  int PointerY{-1};
  bool PointerPressed{false};
  int ActiveTab{0};
  int ObjectPoolTab{0};
  int SectionTab{0};
  PickerMode Picker{PickerMode::None};
  std::string PickerTerrainSlot;
  std::string SelectedEntryId;
  ContentKind SelectedKind{ContentKind::Object};
  std::string SelectedTerrainSlot;
  std::string SelectedOreSlot;
  bool Visible{false};
  bool Built{false};
  bool ContentDirty{true};
};

} // namespace cutum

#endif
