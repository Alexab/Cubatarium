#include "Gui/Screens/WorldGenPaletteScreen.h"

#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Layout/DockedOverlayLayout.h"
#include "Gui/Layout/GuiTooltipLayout.h"
#include "Gui/Preview/ContentPreviewDock.h"
#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiTabBar.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "World/Core/World.h"
#include "WorldGen/Core/WorldGenSets.h"
#include <algorithm>
#include <climits>
#include <iterator>
#include <new>

namespace cutum
{

namespace
{

const char *kObjectPoolLabels[] = {"Vegetation", "Ground cover", "Decoration",
                                   "Structures"};
const char *kTerrainSlots[] = {"surface", "subsurface", "stone", "water",
                               "lava"};
const char *kOreSlots[] = {"ore_coal", "ore_iron"};
constexpr int kPickerZOrder = 100;

const char *DefaultBlockForTerrainSlot(const std::string &slotName)
{
  if (slotName == "surface")
  {
    return "grass";
  }
  if (slotName == "subsurface")
  {
    return "dirt";
  }
  if (slotName == "stone")
  {
    return "stone";
  }
  if (slotName == "water")
  {
    return "water";
  }
  if (slotName == "lava")
  {
    return "lava";
  }
  return nullptr;
}

} // namespace

struct UWorldGenPaletteScreen::SlotAction
{
  UWorldGenPaletteScreen *owner;
  const GridSlot *Slot;
  void operator()() const { owner->ActivateSlot(*Slot); }
};

struct UWorldGenPaletteScreen::GridLayoutAction
{
  UWorldGenPaletteScreen *owner;
  void operator()(UGuiScrollView &view) const { owner->LayoutIconGrid(view); }
};

template<void (UWorldGenPaletteScreen::*Action)()>
struct UWorldGenPaletteScreen::ButtonAction
{
  UWorldGenPaletteScreen *owner;
  void operator()() const { (owner->*Action)(); }
};

struct UWorldGenPaletteScreen::TabAction
{
  enum class Bar { Main, Pool, Section };
  UWorldGenPaletteScreen *owner;
  Bar bar;

  void operator()(int tab) const
  {
    switch (bar)
    {
    case Bar::Main:
      owner->MainTabs->SetActiveTab(owner->ActiveTab);
      owner->RequestedTab = tab;
      break;
    case Bar::Pool:
      owner->ObjectPoolTabs->SetActiveTab(owner->ObjectPoolTab);
      owner->RequestedPoolTab = tab;
      owner->RequestedSectionTab = 0;
      break;
    case Bar::Section:
      owner->SectionTabs->SetActiveTab(owner->SectionTab);
      owner->RequestedPoolTab = owner->ObjectPoolTab;
      owner->RequestedSectionTab = tab;
      break;
    }
    owner->ContentDirty = true;
  }
};

class UWorldGenPaletteScreen::GridSlot final : public UGuiSlot
{
public:
  GridSlot(UWorldGenPaletteScreen &screen, bool picker, ContentKind kind,
           std::string id, std::string tooltip, std::string terrainSlot = {})
      : UGuiSlot(screen.Theme), PickerRow(picker), Kind(kind), Id(std::move(id)),
        Tooltip(std::move(tooltip)), TerrainSlot(std::move(terrainSlot))
  {
    SetOnClick(SlotAction{&screen, this});
  }

  const bool PickerRow;
  const ContentKind Kind;
  const std::string Id;
  std::string Tooltip;
  const std::string TerrainSlot;
};

UWorldGenPaletteScreen::UWorldGenPaletteScreen(UWorld *world,
                                               IUContentCatalog *catalog,
                                               IUGuiIconSource *icons,
                                               UContentPreviewRenderer *previewRenderer)
    : World(world), Catalog(catalog), Icons(icons),
      PreviewRenderer(previewRenderer)
{
}

UWorldGenPaletteScreen::~UWorldGenPaletteScreen() = default;

void UWorldGenPaletteScreen::SetVisible(bool visible)
{
  Visible = visible;
  if (Root)
  {
    Root->SetVisible(visible);
  }
  if (!visible)
  {
    ClosePicker();
    SelectedEntryId.clear();
    SelectedTerrainSlot.clear();
    SelectedOreSlot.clear();
    RequestedTab = ActiveTab;
    RequestedPoolTab = ObjectPoolTab;
    RequestedSectionTab = SectionTab;
    PreviewDirty = true;
  }
  else
  {
    ContentDirty = true;
    RelayoutPanel();
  }
}

void UWorldGenPaletteScreen::Toggle() { SetVisible(!Visible); }

void UWorldGenPaletteScreen::Build(UGuiContext &ctx)
{
  if (PointerPressed)
    return;
  const auto *theme = &ctx.GetTheme();
  auto rootShell = std::make_unique<UGuiPanel>(theme);
  rootShell->SetDrawBackground(false);
  rootShell->SetClipChildren(false);
  auto panel = std::make_unique<UGuiPanel>(theme);
  panel->SetDrawBackground(true);
  panel->SetZOrder(10);
  auto banner = std::make_unique<UGuiLabel>(
      theme, "Changes apply to newly generated chunks only.");

  auto mainTabs = std::make_unique<UGuiTabBar>(theme);
  mainTabs->SetTabs({"Objects", "Terrain", "Ores"});
  mainTabs->SetActiveTab(ActiveTab);
  mainTabs->SetOnTabChanged(TabAction{this, TabAction::Bar::Main});

  auto poolTabs = std::make_unique<UGuiTabBar>(theme);
  poolTabs->SetTabs(
      {kObjectPoolLabels[0], kObjectPoolLabels[1], kObjectPoolLabels[2],
       kObjectPoolLabels[3]});
  poolTabs->SetActiveTab(ObjectPoolTab);
  poolTabs->SetOnTabChanged(TabAction{this, TabAction::Bar::Pool});

  auto sectionTabs = std::make_unique<UGuiTabBar>(theme);
  sectionTabs->SetOnTabChanged(TabAction{this, TabAction::Bar::Section});

  const GridLayoutAction layoutGrid{this};
  auto scroll = std::make_unique<UGuiScrollView>(theme);
  scroll->SetAfterScrollLayout(layoutGrid);

  auto addBtn = std::make_unique<UGuiButton>(theme, "+ Add object");
  addBtn->SetOnClick(ButtonAction<&UWorldGenPaletteScreen::OpenObjectPicker>{this});

  auto resetBtn =
      std::make_unique<UGuiButton>(theme, "Reset section to defaults");
  resetBtn->SetOnClick(ButtonAction<&UWorldGenPaletteScreen::OnResetSection>{this});

  auto tooltip = std::make_unique<UGuiLabel>(theme, "");
  tooltip->SetVisible(false);

  auto pickerPanel = std::make_unique<UGuiPanel>(theme);
  pickerPanel->SetDrawBackground(true);
  pickerPanel->SetVisible(false);
  pickerPanel->SetZOrder(kPickerZOrder);
  auto pickerTitle = std::make_unique<UGuiLabel>(theme, "Pick item");
  auto pickerClose = std::make_unique<UGuiButton>(theme, "Close");
  pickerClose->SetOnClick(ButtonAction<&UWorldGenPaletteScreen::ClosePicker>{this});
  auto pickerScroll = std::make_unique<UGuiScrollView>(theme);
  pickerScroll->SetAfterScrollLayout(layoutGrid);
  auto preview = std::make_unique<UContentPreviewDock>(theme, PreviewRenderer);

  pickerPanel->ReserveChildren(3);
  panel->ReserveChildren(9);
  rootShell->ReserveChildren(2);
  auto *const titleAlias = pickerTitle.get();
  auto *const closeAlias = pickerClose.get();
  auto *const pickerScrollAlias = pickerScroll.get();
  auto *const bannerAlias = banner.get();
  auto *const mainAlias = mainTabs.get();
  auto *const poolAlias = poolTabs.get();
  auto *const sectionAlias = sectionTabs.get();
  auto *const scrollAlias = scroll.get();
  auto *const addAlias = addBtn.get();
  auto *const resetAlias = resetBtn.get();
  auto *const tooltipAlias = tooltip.get();
  auto *const pickerAlias = pickerPanel.get();
  auto *const panelAlias = panel.get();
  pickerPanel->AddChild(std::move(pickerTitle));
  pickerPanel->AddChild(std::move(pickerClose));
  pickerPanel->AddChild(std::move(pickerScroll));
  panel->AddChild(std::move(banner));
  panel->AddChild(std::move(mainTabs));
  panel->AddChild(std::move(poolTabs));
  panel->AddChild(std::move(sectionTabs));
  panel->AddChild(std::move(scroll));
  panel->AddChild(std::move(addBtn));
  panel->AddChild(std::move(resetBtn));
  panel->AddChild(std::move(tooltip));
  panel->AddChild(std::move(pickerPanel));
  rootShell->AddChild(std::move(panel));
  rootShell->AddChild(preview->ReleasePanel());
  PickerTitleLabel = titleAlias;
  PickerCloseButton = closeAlias;
  PickerScroll = pickerScrollAlias;
  BannerLabel = bannerAlias;
  MainTabs = mainAlias;
  ObjectPoolTabs = poolAlias;
  SectionTabs = sectionAlias;
  ContentScroll = scrollAlias;
  AddButton = addAlias;
  ResetButton = resetAlias;
  TooltipLabel = tooltipAlias;
  PickerPanel = pickerAlias;
  Panel = panelAlias;
  Theme = theme;
  Renderer = &ctx.GetRenderer();
  RootShell = rootShell.get();
  PreviewDock = std::move(preview);
  Root = std::move(rootShell);
  Built = true;
  ContentDirty = true;
  PreviewDirty = true;
  ClosePicker();
  Root->SetVisible(Visible);
  RelayoutPanel();
}

void UWorldGenPaletteScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  RelayoutPanel();
}

void UWorldGenPaletteScreen::SetPointerPosition(int x, int y)
{
  PointerX = x;
  PointerY = y;
}

void UWorldGenPaletteScreen::SetPointerPressed(bool pressed)
{
  PointerPressed = pressed;
}

void UWorldGenPaletteScreen::Update(double dt)
{
  if (!Built)
  {
    return;
  }
  if (!PointerPressed)
  {
    try
    {
      if (Visible && ContentDirty)
        RebuildMainContent();
      if (PickerDirty)
        PublishPicker();
      if (PreviewDirty)
      {
        SyncPreviewDock();
        PreviewDirty = false;
      }
    }
    catch (const std::bad_alloc &)
    {
      // The published rows remain valid while preparation is retried.
      return;
    }
  }
  if (Visible)
  {
    PreviewDock->Update(dt);
    UpdateTooltip();
  }
}

void UWorldGenPaletteScreen::RelayoutPanel()
{
  if (!Panel || !Theme)
  {
    return;
  }
  const DockedLayout layout = DockedOverlayLayout::Compute(
      ViewportW, ViewportH, GetContentOffsetX(), GetContentOffsetY(), 65, 28,
      *Theme);
  if (RootShell)
  {
    RootShell->SetBounds(
        {GetContentOffsetX(), GetContentOffsetY(), ViewportW, ViewportH});
  }
  Panel->SetBounds(layout.main);
  if (PreviewDock)
  {
    PreviewDock->Relayout(layout.preview);
  }

  const int panelX = layout.main.X;
  const int panelY = layout.main.Y;
  const int panelW = layout.main.W;
  const int panelH = layout.main.H;

  const int pad = Theme->Padding;
  const int tabH = Theme->TabBarHeight;
  int y = panelY + pad;
  if (BannerLabel)
  {
    BannerLabel->SetBounds({panelX + pad, y, panelW - pad * 2, 24});
    y += 28;
  }
  if (MainTabs)
  {
    MainTabs->SetBounds({panelX + pad, y, panelW - pad * 2, tabH});
    y += tabH + pad;
  }
  const bool objectsTab = ActiveTab == 0;
  if (ObjectPoolTabs)
  {
    ObjectPoolTabs->SetVisible(objectsTab);
    if (objectsTab)
    {
      ObjectPoolTabs->SetBounds({panelX + pad, y, panelW - pad * 2, tabH});
      y += tabH + pad;
    }
  }
  if (SectionTabs)
  {
    const bool showSections = objectsTab && !SectionTabs->GetTabLabels().empty();
    SectionTabs->SetVisible(showSections);
    if (showSections)
    {
      SectionTabs->SetBounds({panelX + pad, y, panelW - pad * 2, tabH});
      y += tabH + pad;
    }
  }
  const int buttonH = tabH;
  const int bottomH = buttonH * 2 + pad;
  if (ContentScroll)
  {
    const int contentH =
        std::max(120, panelH - (y - panelY) - bottomH - pad);
    ContentScroll->SetBounds({panelX + pad, y, panelW - pad * 2, contentH});
    y += contentH + pad;
    ContentScroll->LayoutContent();
  }
  if (AddButton)
  {
    AddButton->SetVisible(objectsTab);
    if (AddButton->IsVisible())
    {
      AddButton->SetBounds({panelX + pad, y, panelW - pad * 2, buttonH});
      y += buttonH + pad / 2;
    }
  }
  if (ResetButton)
  {
    ResetButton->SetBounds({panelX + pad, y, panelW - pad * 2, buttonH});
  }

  if (PickerPanel && PickerPanel->IsVisible())
  {
    const int pickW = panelW * 85 / 100;
    const int pickH = panelH * 85 / 100;
    const int pickX = panelX + (panelW - pickW) / 2;
    const int pickY = panelY + (panelH - pickH) / 2;
    PickerPanel->SetBounds({pickX, pickY, pickW, pickH});
    int py = pickY + pad;
    if (PickerTitleLabel)
    {
      PickerTitleLabel->SetBounds({pickX + pad, py, pickW - pad * 2, 24});
      py += 28;
    }
    if (PickerCloseButton)
    {
      PickerCloseButton->SetBounds({pickX + pad, py, pickW - pad * 2, tabH});
      py += tabH + pad;
    }
    if (PickerScroll)
    {
      PickerScroll->SetBounds(
          {pickX + pad, py, pickW - pad * 2, pickH - (py - pickY) - pad});
      PickerScroll->LayoutContent();
    }
  }
}

std::vector<CatalogEntry>
UWorldGenPaletteScreen::CollectCatalogEntries(ContentKind kind) const
{
  std::vector<CatalogEntry> out;
  if (!Catalog)
  {
    return out;
  }
  for (const std::string &typeId : Catalog->GetTypeIds(kind))
  {
    auto entries = Catalog->GetEntries(kind, typeId);
    out.insert(out.end(), std::make_move_iterator(entries.begin()),
               std::make_move_iterator(entries.end()));
  }
  std::sort(out.begin(), out.end(),
            [](const CatalogEntry &a, const CatalogEntry &b)
            { return a.displayName < b.displayName; });
  return out;
}

std::string
UWorldGenPaletteScreen::DisplayNameForId(ContentKind kind,
                                         const std::string &id,
                                         const std::vector<CatalogEntry> &entries) const
{
  for (const CatalogEntry &entry : entries)
  {
    if (entry.Id == id)
    {
      return entry.displayName;
    }
  }
  if (kind == ContentKind::Block)
  {
    return HumanizeBlockName(id);
  }
  return id;
}

GLuint UWorldGenPaletteScreen::IconForEntry(ContentKind kind,
                                            const std::string &id) const
{
  if (!Icons || id.empty())
  {
    return 0;
  }
  if (kind == ContentKind::Block)
  {
    return Icons->GetBlockIconTexture(id);
  }
  return Icons->GetObjectIconTexture(id);
}

void UWorldGenPaletteScreen::SelectEntry(ContentKind kind, const std::string &id,
                                         const std::string &terrainSlot,
                                         const std::string &oreSlot)
{
  auto nextId = id;
  auto nextTerrain = terrainSlot;
  auto nextOre = oreSlot;
  SelectedKind = kind;
  SelectedEntryId.swap(nextId);
  SelectedTerrainSlot.swap(nextTerrain);
  SelectedOreSlot.swap(nextOre);
  PreviewDirty = true;
  ApplySlotSelection();
}

std::string
UWorldGenPaletteScreen::StoredTerrainBlock(const std::string &slotName) const
{
  if (!World)
  {
    return {};
  }
  const auto it = World->GetWorldGenSets().Terrain.find(slotName);
  if (it == World->GetWorldGenSets().Terrain.end())
  {
    return {};
  }
  return it->second.Block;
}

std::string
UWorldGenPaletteScreen::EffectiveTerrainBlock(const std::string &slotName) const
{
  const std::string stored = StoredTerrainBlock(slotName);
  if (!stored.empty())
  {
    return stored;
  }
  const char *fallback = DefaultBlockForTerrainSlot(slotName);
  return fallback ? std::string(fallback) : std::string();
}

void UWorldGenPaletteScreen::ApplySlotSelection()
{
  for (const auto &child : ContentScroll->Content().GetChildren())
  {
    auto *slot = dynamic_cast<GridSlot *>(child.get());
    if (!slot)
      continue;
    const bool selected = ActiveTab == 1 ? slot->TerrainSlot == SelectedTerrainSlot
        : ActiveTab == 2 ? slot->Id == SelectedOreSlot
        : slot->Kind == SelectedKind && slot->Id == SelectedEntryId;
    slot->SetSelected(selected);
  }
}

void UWorldGenPaletteScreen::RenderPreview()
{
  if (PreviewDock)
  {
    PreviewDock->RenderIfDirty();
  }
}

void UWorldGenPaletteScreen::SyncPreviewDock()
{
  if (!PreviewDock)
  {
    return;
  }
  if (ActiveTab == 1 && !SelectedTerrainSlot.empty())
  {
    const std::string previewBlock = EffectiveTerrainBlock(SelectedTerrainSlot);
    std::string title = SelectedTerrainSlot;
    const std::string stored = StoredTerrainBlock(SelectedTerrainSlot);
    if (!stored.empty())
    {
      title = SelectedTerrainSlot + ": " +
              DisplayNameForId(ContentKind::Block, stored,
                               CollectCatalogEntries(ContentKind::Block));
    }
    else if (!previewBlock.empty())
    {
      title = SelectedTerrainSlot + " (pack default)";
    }
    if (!previewBlock.empty())
    {
      PreviewDock->SetSelection(ContentKind::Block, previewBlock, title);
    }
    else
    {
      PreviewDock->ClearSelection();
    }
    PreviewDock->SetOnChange(ButtonAction<&UWorldGenPaletteScreen::OpenTerrainPicker>{this});
    return;
  }
  if (ActiveTab == 2 && !SelectedOreSlot.empty() && World)
  {
    const std::string blockId = SelectedOreSlot;
    PreviewDock->SetSelection(
        ContentKind::Block, blockId,
        DisplayNameForId(ContentKind::Block, blockId,
                         CollectCatalogEntries(ContentKind::Block)));
    PreviewDock->SetOnChange(ButtonAction<&UWorldGenPaletteScreen::ToggleOre>{this},
                             "Toggle enabled");
    return;
  }
  PreviewDock->SetOnChange(nullptr);
  if (!SelectedEntryId.empty() && PreviewRenderer &&
      PreviewRenderer->SupportsKind(SelectedKind))
  {
    PreviewDock->SetSelection(SelectedKind, SelectedEntryId,
                              DisplayNameForId(SelectedKind, SelectedEntryId,
                                               CollectCatalogEntries(SelectedKind)));
  }
  else
  {
    PreviewDock->ClearSelection();
  }
}

void UWorldGenPaletteScreen::UpdateTooltip()
{
  if (!TooltipLabel || !Theme)
  {
    return;
  }
  const std::string *text = nullptr;
  const bool pickerActive = PickerPanel && PickerPanel->IsVisible();
  UGuiScrollView *scroll = pickerActive ? PickerScroll : ContentScroll;
  if (scroll && scroll->GetBounds().Contains(PointerX, PointerY))
  {
    for (const auto &child : scroll->Content().GetChildren())
    {
      const auto *slot = dynamic_cast<const GridSlot *>(child.get());
      if (slot && slot->IsVisible() &&
          slot->GetBounds().Contains(PointerX, PointerY))
      {
        text = &slot->Tooltip;
        break;
      }
    }
  }

  if (!text || text->empty() || PointerX < 0 || PointerY < 0)
  {
    TooltipLabel->SetVisible(false);
    return;
  }

  const GuiRect viewport{GetContentOffsetX(), GetContentOffsetY(), ViewportW,
                         ViewportH};
  const int textW = MeasureTooltipTextWidth(*text, *Theme, Renderer);
  LayoutTooltipNearPointer(*TooltipLabel, *text, PointerX, PointerY, viewport, *Theme,
                           textW);
}

void UWorldGenPaletteScreen::LayoutIconGrid(UGuiScrollView &scroll)
{
  const auto &children = scroll.Content().GetChildren();
  const GuiRect rect = scroll.Content().GetBounds();
  const int slotSize = Theme->HotbarSlotSize;
  const int stride = std::max(1, slotSize + Theme->HotbarSlotGap);
  const int cols = std::max(1, (rect.W + Theme->HotbarSlotGap) / stride);
  int index = 0;
  for (const auto &child : children)
  {
    if (dynamic_cast<const GridSlot *>(child.get()))
    {
      child->SetBounds({rect.X + index % cols * stride,
                        rect.Y + index / cols * stride, slotSize, slotSize});
      ++index;
    }
    else
    {
      child->SetBounds({rect.X, rect.Y, rect.W, child->GetPreferredHeight()});
    }
  }
}

std::unordered_map<std::string, WorldGenObjectSection> *
UWorldGenPaletteScreen::ObjectPool(int tab) const
{
  if (!World)
    return nullptr;
  auto &sets = World->GetWorldGenSets();
  switch (tab)
  {
  case 0: return &sets.VegetationSections;
  case 1: return &sets.GroundCoverSections;
  case 2: return &sets.DecorationSections;
  case 3: return &sets.StructureSections;
  default: return nullptr;
  }
}

void UWorldGenPaletteScreen::RebuildMainContent()
{
  if (!World)
  {
    ContentDirty = false;
    return;
  }

  std::vector<std::unique_ptr<UGuiWidget>> rows;
  std::vector<std::string> sectionKeys;
  std::vector<bool> sectionEnabled;
  int nextSection = RequestedSectionTab;
  auto *pool = ObjectPool(RequestedPoolTab);

  if (RequestedTab == 0 && pool)
  {
    sectionKeys.reserve(std::max<size_t>(1, pool->size()));
    for (const auto &section : *pool)
      sectionKeys.push_back(section.first);
    if (sectionKeys.empty())
      sectionKeys.push_back("default");
    std::sort(sectionKeys.begin(), sectionKeys.end());
    sectionEnabled.resize(sectionKeys.size(), true);
    if (nextSection >= static_cast<int>(sectionKeys.size()))
      nextSection = 0;
    const auto section = pool->find(sectionKeys[nextSection]);
    if (section != pool->end() && !section->second.Entries.empty())
    {
      const auto objects = CollectCatalogEntries(ContentKind::Object);
      const auto blocks = CollectCatalogEntries(ContentKind::Block);
      rows.reserve(section->second.Entries.size());
      for (const auto &entry : section->second.Entries)
      {
        const bool scatter = entry.Mode == ObjectFeatureRuleMode::ScatterBlocks;
        const auto kind = scatter ? ContentKind::Block : ContentKind::Object;
        const auto &id = scatter ? entry.Scatter.BlockName : entry.ObjectName;
        if (id.empty())
          continue;
        const auto label = DisplayNameForId(kind, id, scatter ? blocks : objects);
        auto tooltip = label + "\nw" + std::to_string(entry.Weight);
        if (entry.Spacing > 0)
          tooltip += " s" + std::to_string(entry.Spacing);
        auto row = std::make_unique<GridSlot>(*this, false, kind, id, std::move(tooltip));
        const GLuint icon = IconForEntry(kind, id);
        row->SetIconTexture(icon);
        if (!icon)
          row->SetLabel(label);
        rows.push_back(std::move(row));
      }
    }
  }
  else if (RequestedTab == 1)
  {
    const auto blocks = CollectCatalogEntries(ContentKind::Block);
    rows.reserve(std::size(kTerrainSlots));
    for (const char *name : kTerrainSlots)
    {
      const std::string terrainSlot = name;
      auto stored = StoredTerrainBlock(terrainSlot);
      const auto effective = stored.empty() ? DefaultBlockForTerrainSlot(terrainSlot) : stored;
      const auto label = DisplayNameForId(ContentKind::Block, effective, blocks);
      auto tooltip = terrainSlot + (stored.empty() ? "\n(pack default: " + label + ")" : "\n" + label);
      auto row = std::make_unique<GridSlot>(*this, false, ContentKind::Block,
                                            std::move(stored), std::move(tooltip), terrainSlot);
      row->SetIconTexture(IconForEntry(ContentKind::Block, effective));
      rows.push_back(std::move(row));
    }
  }
  else if (RequestedTab == 2)
  {
    const auto &ores = World->GetWorldGenSets().Ores;
    const auto blocks = CollectCatalogEntries(ContentKind::Block);
    rows.reserve(std::size(kOreSlots));
    for (const char *name : kOreSlots)
    {
      const std::string oreSlot = name;
      const auto it = ores.find(oreSlot);
      const bool enabled = it == ores.end() || it->second.Enabled;
      auto tooltip = oreSlot + "\n" + DisplayNameForId(ContentKind::Block, oreSlot, blocks) +
                     "\n" + (enabled ? "Enabled" : "Disabled");
      auto row = std::make_unique<GridSlot>(*this, false, ContentKind::Block,
                                            oreSlot, std::move(tooltip));
      row->SetIconTexture(IconForEntry(ContentKind::Block, oreSlot));
      row->SetDimmed(!enabled);
      rows.push_back(std::move(row));
    }
  }

  if (rows.empty())
    rows.push_back(std::make_unique<UGuiLabel>(Theme, "No entries — click + Add object"));

  // Keep the default section available for edits. This is the last fallible
  // operation before publication, and a refused insertion leaves the pool intact.
  if (RequestedTab == 0 && pool && pool->empty())
    pool->try_emplace("default");

  if (RequestedTab != ActiveTab)
  {
    SelectedEntryId.clear();
    SelectedTerrainSlot.clear();
    SelectedOreSlot.clear();
    PreviewDirty = true;
  }
  if (RequestedTab != ActiveTab || RequestedPoolTab != ObjectPoolTab ||
      nextSection != SectionTab)
    ClosePicker();

  ActiveTab = RequestedTab;
  ObjectPoolTab = RequestedPoolTab;
  SectionTab = RequestedSectionTab = nextSection;
  MainTabs->SetActiveTab(ActiveTab);
  ObjectPoolTabs->SetActiveTab(ObjectPoolTab);
  SectionTabs->SetTabs(std::move(sectionKeys), std::move(sectionEnabled));
  SectionTabs->SetActiveTab(SectionTab);
  ContentScroll->Content().Children.swap(rows);
  ContentScroll->SetScrollY(0);
  RelayoutPanel();
  ApplySlotSelection();
  ContentDirty = false;
}

void UWorldGenPaletteScreen::OpenObjectPicker()
{
  if (ActiveTab != 0)
    return;
  RequestedPicker = PickerMode::Object;
  RequestedTerrainSlot.clear();
  PickerDirty = true;
}

void UWorldGenPaletteScreen::OpenTerrainPicker()
{
  if (ActiveTab != 1 || SelectedTerrainSlot.empty())
    return;
  RequestedTerrainSlot = SelectedTerrainSlot;
  RequestedPicker = PickerMode::TerrainSlot;
  PickerDirty = true;
}

void UWorldGenPaletteScreen::ToggleOre()
{
  if (ActiveTab != 2 || !World || SelectedOreSlot.empty())
    return;
  auto &ore = World->GetWorldGenSets().Ores[SelectedOreSlot];
  ore.Enabled = !ore.Enabled;
  SaveSets();
  ContentDirty = true;
}

void UWorldGenPaletteScreen::PublishPicker()
{
  if (RequestedPicker == PickerMode::None)
  {
    std::vector<std::unique_ptr<UGuiWidget>>().swap(PickerScroll->Content().Children);
    PickerDirty = false;
    return;
  }

  auto target = RequestedTerrainSlot;
  const bool terrain = RequestedPicker == PickerMode::TerrainSlot;
  std::string title = terrain ? "Pick block for " + target : "Pick object to add";
  const auto kind = terrain ? ContentKind::Block : ContentKind::Object;
  std::vector<std::unique_ptr<UGuiWidget>> rows;
  auto entries = CollectCatalogEntries(kind);
  rows.reserve(std::max<size_t>(1, entries.size()));
  for (auto &entry : entries)
  {
    const auto icon = IconForEntry(kind, entry.Id);
    auto row = std::make_unique<GridSlot>(*this, true, kind, std::move(entry.Id),
                                          std::move(entry.displayName));
    row->SetIconTexture(icon);
    if (!icon)
      row->SetLabel(row->Tooltip);
    rows.push_back(std::move(row));
  }
  if (rows.empty())
    rows.push_back(std::make_unique<UGuiLabel>(Theme, "No items in catalog"));

  PickerTitleLabel->Text.swap(title);
  PickerTerrainSlot.swap(target);
  Picker = RequestedPicker;
  PickerScroll->Content().Children.swap(rows);
  PickerScroll->SetScrollY(0);
  PickerPanel->SetVisible(true);
  RelayoutPanel();
  PickerDirty = false;
}

void UWorldGenPaletteScreen::ClosePicker()
{
  Picker = RequestedPicker = PickerMode::None;
  PickerTerrainSlot.clear();
  RequestedTerrainSlot.clear();
  PickerDirty = true;
  if (PickerPanel)
  {
    PickerPanel->SetVisible(false);
    PickerPanel->SetBounds({0, 0, 0, 0});
  }
}

void UWorldGenPaletteScreen::ActivateSlot(const GridSlot &row)
{
  if (!Visible)
    return;
  if (row.PickerRow)
  {
    if (Picker != PickerMode::None)
      OnPickerPick(row.Id);
    return;
  }
  try
  {
    const std::string empty;
    SelectEntry(row.Kind, row.Id, row.TerrainSlot, ActiveTab == 2 ? row.Id : empty);
  }
  catch (const std::bad_alloc &)
  {
    // A refused selection leaves the active row and preview in place.
  }
}

void UWorldGenPaletteScreen::OnPickerPick(const std::string &id)
{
  if (!World || id.empty())
  {
    ClosePicker();
    return;
  }

  if (Picker == PickerMode::Object)
  {
    auto *pool = ObjectPool(ObjectPoolTab);
    const auto &keys = SectionTabs->GetTabLabels();
    if (!pool || SectionTab >= static_cast<int>(keys.size()))
      return;
    const auto section = pool->find(keys[SectionTab]);
    if (section == pool->end())
    {
      // The model can change independently; refresh the section view.
      ClosePicker();
      ContentDirty = true;
      return;
    }
    WorldGenObjectEntry entry;
    entry.ObjectName = id;
    entry.Weight = 1;
    entry.Spacing = 32;
    section->second.Entries.push_back(std::move(entry));
  }
  else
  {
    auto nextBlock = id;
    auto nextSelected = id;
    auto nextTerrain = PickerTerrainSlot;
    auto &slot = World->GetWorldGenSets().Terrain.try_emplace(PickerTerrainSlot).first->second;
    slot.Block.swap(nextBlock);
    SelectedTerrainSlot.swap(nextTerrain);
    SelectedEntryId.swap(nextSelected);
    SelectedKind = ContentKind::Block;
    PreviewDirty = true;
  }

  ClosePicker();
  ContentDirty = true;
  SaveSets();
}

WorldGenPaletteGridStorage UWorldGenPaletteScreen::InspectGridStorage() const noexcept
{
  WorldGenPaletteGridStorage out;
  if (!Built)
    return out;
  const auto measure = [&](const UGuiScrollView &view, bool picker)
  {
    StlSourceStorage storage;
    const auto &children = view.Content().GetChildren();
    storage.Buffer(children.capacity(), sizeof(children.front()));
    for (const auto &child : children)
    {
      if (!child)
      {
        storage.bytes.supported = false;
        continue;
      }
      if (const auto *row = dynamic_cast<const GridSlot *>(child.get()))
      {
        const auto source = InspectKnownOwnerCallback<SlotAction>(row->OnClick, this, storage.bytes.supported);
        const auto *action = row->OnClick.target<SlotAction>();
        if (!source.supported || !action || action->Slot != row ||
            row->PickerRow != picker || row->Theme != Theme ||
            row->OnActivate || row->OnBeginDrag || !row->GetChildren().empty())
          storage.bytes.supported = false;
        storage.Add(sizeof(GridSlot));
        storage.Add(source.requests);
        storage.Text(row->Id);
        storage.Text(row->Tooltip);
        storage.Text(row->TerrainSlot);
        storage.Text(row->Label);
        storage.Text(row->CornerHint);
        storage.Text(row->CountHint);
      }
      else if (const auto *hint = dynamic_cast<const UGuiLabel *>(child.get());
               hint && typeid(*hint) == typeid(UGuiLabel) && hint->Theme == Theme &&
               hint->GetChildren().empty())
      {
        storage.Add(sizeof(UGuiLabel));
        storage.Text(hint->GetText());
      }
      else
        storage.bytes.supported = false;
      storage.Buffer(child->GetChildren().capacity(), sizeof(children.front()));
    }
    return storage.bytes;
  };
  const auto content = measure(*ContentScroll, false);
  const auto picker = measure(*PickerScroll, true);
  StlSourceStorage tabs, title;
  const auto &labels = SectionTabs->GetTabLabels();
  tabs.Buffer(labels.capacity(), sizeof(std::string));
  tabs.Buffer(SectionTabs->GetEnabledTabs().capacity() / CHAR_BIT, 1);
  for (const auto &label : labels)
    tabs.Text(label);
  title.Text(PickerTitleLabel->GetText());
  out.contentBytes = content.requested;
  out.pickerBytes = picker.requested;
  out.sectionTabBytes = tabs.bytes.requested;
  out.pickerTitleBytes = title.bytes.requested;
  out.supported = content.supported && picker.supported && tabs.bytes.supported &&
                  title.bytes.supported;
  return out;
}

WorldGenPaletteStorage UWorldGenPaletteScreen::InspectOwnedStorage(
    const UGuiContext &context, const UWorld *world, const IUContentCatalog *catalog,
    const IUGuiIconSource *icons, const UContentPreviewRenderer *preview) const noexcept
{
  WorldGenPaletteStorage out;
  out.present = true;
  out.visible = Visible;
  const auto *theme = &context.GetTheme();
  if (typeid(*this) != typeid(UWorldGenPaletteScreen) || !Built ||
      Context != &context || Theme != theme || Metrics.Theme != theme ||
      !Renderer || Renderer != context.Renderer.get() || !world || World != world ||
      !catalog || Catalog != catalog || Icons != icons || PreviewRenderer != preview ||
      !Root || Root.get() != RootShell || typeid(*Root) != typeid(UGuiPanel) ||
      RootShell->Theme != theme || Root->IsVisible() != Visible || !PreviewDock)
    return out;

  const auto matches = [theme](const auto &child, const auto *alias)
  {
    using Widget = std::remove_cv_t<std::remove_pointer_t<decltype(alias)>>;
    return child && child.get() == alias && typeid(*child) == typeid(Widget) &&
           alias->Theme == theme;
  };
  const auto &outer = Root->GetChildren();
  if (outer.size() != 2 || !matches(outer[0], Panel) || !outer[1])
    return out;
  const auto &main = Panel->GetChildren();
  if (main.size() != 9 || !matches(main[0], BannerLabel) || !matches(main[1], MainTabs) ||
      !matches(main[2], ObjectPoolTabs) || !matches(main[3], SectionTabs) ||
      !matches(main[4], ContentScroll) || !matches(main[5], AddButton) ||
      !matches(main[6], ResetButton) || !matches(main[7], TooltipLabel) ||
      !matches(main[8], PickerPanel))
    return out;
  const auto &picker = PickerPanel->GetChildren();
  if (picker.size() != 3 || !matches(picker[0], PickerTitleLabel) ||
      !matches(picker[1], PickerCloseButton) || !matches(picker[2], PickerScroll) ||
      PickerPanel->IsVisible() != (Picker != PickerMode::None) ||
      ContentScroll->Content().Theme != theme || PickerScroll->Content().Theme != theme)
    return out;
  for (std::size_t i = 0; i + 1 < main.size(); ++i)
    if (!main[i]->GetChildren().empty())
      return out;
  for (const auto &child : picker)
    if (!child->GetChildren().empty())
      return out;

  const auto dock = PreviewDock->InspectHostedStorage(*theme, preview, *outer[1]);
  out.grid = InspectGridStorage();
  if (!dock.supported || !out.grid.supported)
    return out;

  const bool profile = StlSourceStorage::SupportedProfile();
  StlSourceStorage children, tabs, text, callbacks, total;
  bool actionsKnown = true;
  const auto requiredAction = [&](const CallbackStorageSnapshot &snapshot)
  {
    actionsKnown &= snapshot.present && snapshot.supported;
    callbacks.Add(snapshot.requests);
  };
  for (const auto *scroll : {ContentScroll, PickerScroll})
    requiredAction(InspectKnownOwnerCallback<GridLayoutAction>(scroll->AfterScrollLayout, this, profile));
  const auto tabAction = [&](const UGuiTabBar &bar, TabAction::Bar role)
  {
    const auto *action = bar.OnTabChanged.target<TabAction>();
    actionsKnown &= action && action->bar == role;
    requiredAction(InspectKnownOwnerCallback<TabAction>(bar.OnTabChanged, this, profile));
  };
  tabAction(*MainTabs, TabAction::Bar::Main);
  tabAction(*ObjectPoolTabs, TabAction::Bar::Pool);
  tabAction(*SectionTabs, TabAction::Bar::Section);
  requiredAction(InspectKnownOwnerCallback<ButtonAction<&UWorldGenPaletteScreen::OpenObjectPicker>>(
      AddButton->OnClick, this, profile));
  requiredAction(InspectKnownOwnerCallback<ButtonAction<&UWorldGenPaletteScreen::OnResetSection>>(
      ResetButton->OnClick, this, profile));
  requiredAction(InspectKnownOwnerCallback<ButtonAction<&UWorldGenPaletteScreen::ClosePicker>>(
      PickerCloseButton->OnClick, this, profile));
  const auto &change = PreviewDock->OnChange;
  if (change)
  {
    using TerrainAction = ButtonAction<&UWorldGenPaletteScreen::OpenTerrainPicker>;
    using OreAction = ButtonAction<&UWorldGenPaletteScreen::ToggleOre>;
    if (change.target<TerrainAction>())
      requiredAction(InspectKnownOwnerCallback<TerrainAction>(change, this, profile));
    else
      requiredAction(InspectKnownOwnerCallback<OreAction>(change, this, profile));
  }
  if (!actionsKnown)
    return out;

  const auto countChildren = [&](const UGuiWidget &widget)
  {
    children.Buffer(widget.GetChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  };
  countChildren(*Root);
  countChildren(*Panel);
  for (const auto &child : main)
    countChildren(*child);
  for (const auto &child : picker)
    countChildren(*child);
  for (const auto *bar : {MainTabs, ObjectPoolTabs})
  {
    tabs.Buffer(bar->GetTabLabels().capacity(), sizeof(std::string));
    tabs.Buffer(bar->GetEnabledTabs().capacity() / CHAR_BIT, 1);
    for (const auto &label : bar->GetTabLabels())
      tabs.Text(label);
  }
  tabs.Add(out.grid.sectionTabBytes);
  for (const auto *label : {BannerLabel, TooltipLabel})
    text.Text(label->GetText());
  for (const auto *button : {AddButton, ResetButton, PickerCloseButton})
    text.Text(button->GetLabel());
  for (const auto *value : {&SelectedEntryId, &SelectedTerrainSlot, &SelectedOreSlot,
                            &PickerTerrainSlot, &RequestedTerrainSlot})
    text.Text(*value);
  text.Add(out.grid.pickerTitleBytes);

  out.bindingsMatch = true;
  out.screenBytes = sizeof(*this);
  out.widgetBytes = 3 * sizeof(UGuiPanel) + 3 * sizeof(UGuiLabel) + 3 * sizeof(UGuiButton) +
                    3 * sizeof(UGuiTabBar) + 2 * sizeof(UGuiScrollView);
  out.widgets = 2 + main.size() + picker.size() + dock.widgets +
                ContentScroll->Content().GetChildren().size() + PickerScroll->Content().GetChildren().size();
  out.childrenBytes = children.bytes.requested;
  out.tabBytes = tabs.bytes.requested;
  out.textBytes = text.bytes.requested;
  out.callbackBytes = callbacks.bytes.requested;
  out.previewBytes = dock.requested;
  out.previewTexturePresent = dock.texturePresent;
  for (const auto bytes : {out.screenBytes, out.widgetBytes, out.childrenBytes, out.tabBytes,
                          out.textBytes, out.callbackBytes, out.previewBytes,
                          out.grid.contentBytes, out.grid.pickerBytes})
    total.Add(bytes);
  out.totalBytes = total.bytes.requested;
  out.supported = total.bytes.supported && children.bytes.supported && tabs.bytes.supported &&
                  text.bytes.supported && callbacks.bytes.supported;
  return out;
}

void UWorldGenPaletteScreen::SaveSets()
{
  if (!World)
  {
    return;
  }
  World->SetWorldGenSets(World->GetWorldGenSets());
  World->SaveWorldGenSetsToDisk();
}

void UWorldGenPaletteScreen::OnResetSection()
{
  if (!World)
  {
    return;
  }
  WorldGenSets defaults = BuildDefaultWorldGenSets();
  WorldGenSets sets = World->GetWorldGenSets();
  if (ActiveTab == 0)
  {
    switch (ObjectPoolTab)
    {
    case 0:
      sets.VegetationSections = defaults.VegetationSections;
      break;
    case 1:
      sets.GroundCoverSections = defaults.GroundCoverSections;
      break;
    case 2:
      sets.DecorationSections = defaults.DecorationSections;
      break;
    case 3:
      sets.StructureSections = defaults.StructureSections;
      break;
    default:
      break;
    }
  }
  else if (ActiveTab == 1)
  {
    sets.Terrain.clear();
  }
  else
  {
    sets.Ores.clear();
  }
  World->SetWorldGenSets(std::move(sets));
  SaveSets();
  ContentDirty = true;
}

} // namespace cutum
