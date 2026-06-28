#include "Gui/Screens/WorldGenPaletteScreen.h"

#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IGuiIconSource.h"
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
#include <sstream>

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

std::string EntryHint(const WorldGenObjectEntry &entry)
{
  std::ostringstream oss;
  oss << "w" << entry.Weight;
  if (entry.Spacing > 0)
  {
    oss << " s" << entry.Spacing;
  }
  return oss.str();
}

} // namespace

UWorldGenPaletteScreen::UWorldGenPaletteScreen(UWorld *world,
                                               IContentCatalog *catalog,
                                               IGuiIconSource *icons,
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
    if (PreviewDock)
    {
      PreviewDock->ClearSelection();
      PreviewDock->SetOnChange(nullptr);
    }
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
  Theme = &ctx.GetTheme();
  Renderer = &ctx.GetRenderer();

  auto rootShell = std::make_unique<UGuiPanel>(Theme);
  rootShell->SetDrawBackground(false);
  rootShell->SetClipChildren(false);
  RootShell = rootShell.get();

  auto panel = std::make_unique<UGuiPanel>(Theme);
  panel->SetDrawBackground(true);
  panel->SetZOrder(10);
  Panel = panel.get();

  auto banner = std::make_unique<UGuiLabel>(
      Theme, "Changes apply to newly generated chunks only.");
  BannerLabel = banner.get();

  auto mainTabs = std::make_unique<UGuiTabBar>(Theme);
  mainTabs->SetTabs({"Objects", "Terrain", "Ores"});
  MainTabs = mainTabs.get();
  mainTabs->SetOnTabChanged(
      [this](int tab)
      {
        ActiveTab = tab;
        ClosePicker();
        SelectedEntryId.clear();
        SelectedTerrainSlot.clear();
        SelectedOreSlot.clear();
        SyncPreviewDock();
        ContentDirty = true;
      });

  auto poolTabs = std::make_unique<UGuiTabBar>(Theme);
  poolTabs->SetTabs(
      {kObjectPoolLabels[0], kObjectPoolLabels[1], kObjectPoolLabels[2],
       kObjectPoolLabels[3]});
  ObjectPoolTabs = poolTabs.get();
  poolTabs->SetOnTabChanged(
      [this](int tab)
      {
        ObjectPoolTab = tab;
        SectionTab = 0;
        EnsureActiveSection();
        ContentDirty = true;
      });

  auto sectionTabs = std::make_unique<UGuiTabBar>(Theme);
  SectionTabs = sectionTabs.get();
  sectionTabs->SetOnTabChanged(
      [this](int tab)
      {
        SectionTab = tab;
        ContentDirty = true;
      });

  auto scroll = std::make_unique<UGuiScrollView>(Theme);
  ContentScroll = scroll.get();

  auto addBtn = std::make_unique<UGuiButton>(Theme, "+ Add");
  addBtn->SetOnClick([this]() { OpenObjectPicker(); });
  AddButton = addBtn.get();

  auto resetBtn =
      std::make_unique<UGuiButton>(Theme, "Reset section to defaults");
  resetBtn->SetOnClick([this]() { OnResetSection(); });
  ResetButton = resetBtn.get();

  panel->AddChild(std::move(banner));
  panel->AddChild(std::move(mainTabs));
  panel->AddChild(std::move(poolTabs));
  panel->AddChild(std::move(sectionTabs));
  panel->AddChild(std::move(scroll));
  panel->AddChild(std::move(addBtn));
  panel->AddChild(std::move(resetBtn));

  auto tooltip = std::make_unique<UGuiLabel>(Theme, "");
  tooltip->SetVisible(false);
  TooltipLabel = tooltip.get();
  panel->AddChild(std::move(tooltip));

  auto pickerPanel = std::make_unique<UGuiPanel>(Theme);
  pickerPanel->SetDrawBackground(true);
  pickerPanel->SetVisible(false);
  pickerPanel->SetZOrder(kPickerZOrder);
  PickerPanel = pickerPanel.get();

  auto pickerTitle = std::make_unique<UGuiLabel>(Theme, "Pick item");
  PickerTitleLabel = pickerTitle.get();

  auto pickerClose = std::make_unique<UGuiButton>(Theme, "Close");
  pickerClose->SetOnClick([this]() { ClosePicker(); });
  PickerCloseButton = pickerClose.get();

  auto pickerScroll = std::make_unique<UGuiScrollView>(Theme);
  PickerScroll = pickerScroll.get();

  pickerPanel->AddChild(std::move(pickerTitle));
  pickerPanel->AddChild(std::move(pickerClose));
  pickerPanel->AddChild(std::move(pickerScroll));

  panel->AddChild(std::move(pickerPanel));
  rootShell->AddChild(std::move(panel));

  PreviewDock =
      std::make_unique<UContentPreviewDock>(Theme, PreviewRenderer);
  rootShell->AddChild(PreviewDock->ReleasePanel());

  Root = std::move(rootShell);
  Built = true;
  Root->SetVisible(false);
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
  (void)dt;
  if (!Visible || !Built)
  {
    return;
  }
  if (ContentDirty)
  {
    RebuildMainContent();
    ContentDirty = false;
  }
  if (PreviewDock)
  {
    PreviewDock->Update(dt);
  }
  UpdateTooltip();
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
    const bool showSections = objectsTab && !SectionKeysForPool().empty();
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
    if (!ContentSlots.empty())
    {
      ContentScroll->LayoutContent();
      LayoutContentIconGrid();
    }
  }
  if (AddButton)
  {
    AddButton->SetVisible(ActiveTab == 0 || ActiveTab == 1);
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
      if (!PickerSlots.empty())
      {
        PickerScroll->LayoutContent();
        LayoutPickerIconGrid();
      }
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
    const auto entries = Catalog->GetEntries(kind, typeId);
    out.insert(out.end(), entries.begin(), entries.end());
  }
  std::sort(out.begin(), out.end(),
            [](const CatalogEntry &a, const CatalogEntry &b)
            { return a.displayName < b.displayName; });
  return out;
}

std::string
UWorldGenPaletteScreen::DisplayNameForId(ContentKind kind,
                                         const std::string &id) const
{
  for (const CatalogEntry &entry : CollectCatalogEntries(kind))
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
                                         const std::string &displayName,
                                         const std::string &terrainSlot,
                                         const std::string &oreSlot)
{
  SelectedKind = kind;
  SelectedEntryId = id;
  SelectedTerrainSlot = terrainSlot;
  SelectedOreSlot = oreSlot;
  SyncPreviewDock();
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

std::string
UWorldGenPaletteScreen::TerrainSlotTooltip(const std::string &slotName) const
{
  const std::string stored = StoredTerrainBlock(slotName);
  const std::string effective = EffectiveTerrainBlock(slotName);
  std::string text = slotName;
  if (stored.empty())
  {
    text += "\n(pack default";
    if (!effective.empty())
    {
      text += ": " + DisplayNameForId(ContentKind::Block, effective);
    }
    text += ")";
  }
  else
  {
    text += "\n" + DisplayNameForId(ContentKind::Block, stored);
  }
  return text;
}

std::string UWorldGenPaletteScreen::OreSlotTooltip(const std::string &oreSlot,
                                                   bool enabled) const
{
  return oreSlot + "\n" + DisplayNameForId(ContentKind::Block, oreSlot) + "\n" +
         (enabled ? "Enabled" : "Disabled");
}

void UWorldGenPaletteScreen::ApplySlotSelection()
{
  for (size_t i = 0; i < ContentSlots.size(); ++i)
  {
    UGuiSlot *slot = ContentSlots[i];
    if (!slot || i >= ContentSlotKinds.size() || i >= ContentSlotIds.size())
    {
      continue;
    }
    bool selected = false;
    if (ActiveTab == 1 && i < ContentSlotTerrainSlots.size())
    {
      selected = ContentSlotTerrainSlots[i] == SelectedTerrainSlot;
    }
    else if (ActiveTab == 2)
    {
      selected = ContentSlotIds[i] == SelectedOreSlot;
    }
    else
    {
      selected = ContentSlotKinds[i] == SelectedKind &&
                 ContentSlotIds[i] == SelectedEntryId;
    }
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
              DisplayNameForId(ContentKind::Block, stored);
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
    PreviewDock->SetOnChange(
        [this]() { OpenTerrainPicker(SelectedTerrainSlot); });
    return;
  }
  if (ActiveTab == 2 && !SelectedOreSlot.empty() && World)
  {
    const std::string blockId = SelectedOreSlot;
    PreviewDock->SetSelection(
        ContentKind::Block, blockId,
        DisplayNameForId(ContentKind::Block, blockId));
    PreviewDock->SetOnChange(
        [this]()
        {
          if (!World || SelectedOreSlot.empty())
          {
            return;
          }
          WorldGenOreSlot &ore = World->GetWorldGenSets().Ores[SelectedOreSlot];
          ore.Enabled = !ore.Enabled;
          SaveSets();
          ContentDirty = true;
        },
        "Toggle enabled");
    return;
  }
  PreviewDock->SetOnChange(nullptr);
  if (!SelectedEntryId.empty() && PreviewRenderer &&
      PreviewRenderer->SupportsKind(SelectedKind))
  {
    PreviewDock->SetSelection(SelectedKind, SelectedEntryId,
                              DisplayNameForId(SelectedKind, SelectedEntryId));
  }
  else
  {
    PreviewDock->ClearSelection();
  }
}

std::string UWorldGenPaletteScreen::LabelAt(size_t index, bool picker) const
{
  const auto &labels = picker ? PickerSlotLabels : ContentSlotLabels;
  if (index < labels.size() && !labels[index].empty())
  {
    return labels[index];
  }
  return {};
}

void UWorldGenPaletteScreen::UpdateTooltip()
{
  if (!TooltipLabel || !Theme)
  {
    return;
  }
  std::string text;
  int tipX = PointerX;
  int tipY = PointerY;
  const bool pickerActive = PickerPanel && PickerPanel->IsVisible();
  UGuiScrollView *scroll = pickerActive ? PickerScroll : ContentScroll;
  const auto &slots = pickerActive ? PickerSlots : ContentSlots;

  if (PointerX >= 0 && PointerY >= 0 && scroll)
  {
    for (size_t i = 0; i < slots.size(); ++i)
    {
      UGuiSlot *slot = slots[i];
      if (slot && slot->IsVisible() &&
          slot->GetBounds().Contains(PointerX, PointerY))
      {
        text = LabelAt(i, pickerActive);
        if (!pickerActive && i < ContentSlotHints.size() &&
            !ContentSlotHints[i].empty())
        {
          text += "\n" + ContentSlotHints[i];
        }
        break;
      }
    }
  }

  if (text.empty() || tipX < 0 || tipY < 0)
  {
    TooltipLabel->SetVisible(false);
    return;
  }

  const GuiRect viewport{GetContentOffsetX(), GetContentOffsetY(), ViewportW,
                         ViewportH};
  const int textW = MeasureTooltipTextWidth(text, *Theme, Renderer);
  LayoutTooltipNearPointer(*TooltipLabel, text, tipX, tipY, viewport, *Theme,
                           textW);
}

void UWorldGenPaletteScreen::LayoutContentIconGrid()
{
  if (!ContentScroll || !Theme)
  {
    return;
  }
  const GuiRect contentRect = ContentScroll->Content().GetBounds();
  const int slotSize = Theme->HotbarSlotSize;
  const int gap = Theme->HotbarSlotGap;
  const int viewportW = std::max(1, contentRect.W);
  const int viewportH = std::max(1, ContentScroll->GetBounds().H);
  const int cols =
      std::max(1, (viewportW + gap) / std::max(1, slotSize + gap));
  const int contentTop = contentRect.Y;

  int x = contentRect.X;
  int y = contentTop;
  int col = 0;
  for (UGuiSlot *slot : ContentSlots)
  {
    if (!slot)
    {
      continue;
    }
    slot->SetBounds({x, y, slotSize, slotSize});
    col++;
    if (col >= cols)
    {
      col = 0;
      x = contentRect.X;
      y += slotSize + gap;
    }
    else
    {
      x += slotSize + gap;
    }
  }

  const int rows = ContentSlots.empty()
                       ? 0
                       : static_cast<int>((ContentSlots.size() + cols - 1) / cols);
  const int contentHeight =
      std::max(viewportH, rows * (slotSize + gap) - (rows > 0 ? gap : 0) + 8);
  ContentScroll->Content().SetBounds(
      {contentRect.X, contentTop, viewportW, contentHeight});
}

void UWorldGenPaletteScreen::LayoutPickerIconGrid()
{
  if (!PickerScroll || !Theme)
  {
    return;
  }
  const GuiRect contentRect = PickerScroll->Content().GetBounds();
  const int slotSize = Theme->HotbarSlotSize;
  const int gap = Theme->HotbarSlotGap;
  const int viewportW = std::max(1, contentRect.W);
  const int viewportH = std::max(1, PickerScroll->GetBounds().H);
  const int cols =
      std::max(1, (viewportW + gap) / std::max(1, slotSize + gap));
  const int contentTop = contentRect.Y;

  int x = contentRect.X;
  int y = contentTop;
  int col = 0;
  for (UGuiSlot *slot : PickerSlots)
  {
    if (!slot)
    {
      continue;
    }
    slot->SetBounds({x, y, slotSize, slotSize});
    col++;
    if (col >= cols)
    {
      col = 0;
      x = contentRect.X;
      y += slotSize + gap;
    }
    else
    {
      x += slotSize + gap;
    }
  }

  const int rows = PickerSlots.empty()
                       ? 0
                       : static_cast<int>((PickerSlots.size() + cols - 1) / cols);
  const int contentHeight =
      std::max(viewportH, rows * (slotSize + gap) - (rows > 0 ? gap : 0) + 8);
  PickerScroll->Content().SetBounds(
      {contentRect.X, contentTop, viewportW, contentHeight});
}

std::unordered_map<std::string, WorldGenObjectSection> *
UWorldGenPaletteScreen::ActiveObjectPool()
{
  if (!World)
  {
    return nullptr;
  }
  switch (ObjectPoolTab)
  {
  case 0:
    return &World->GetWorldGenSets().VegetationSections;
  case 1:
    return &World->GetWorldGenSets().GroundCoverSections;
  case 2:
    return &World->GetWorldGenSets().DecorationSections;
  case 3:
    return &World->GetWorldGenSets().StructureSections;
  default:
    return nullptr;
  }
}

std::vector<std::string> UWorldGenPaletteScreen::SectionKeysForPool() const
{
  std::vector<std::string> keys;
  if (!World)
  {
    return keys;
  }
  const WorldGenSets &sets = World->GetWorldGenSets();
  const std::unordered_map<std::string, WorldGenObjectSection> *pool = nullptr;
  switch (ObjectPoolTab)
  {
  case 0:
    pool = &sets.VegetationSections;
    break;
  case 1:
    pool = &sets.GroundCoverSections;
    break;
  case 2:
    pool = &sets.DecorationSections;
    break;
  case 3:
    pool = &sets.StructureSections;
    break;
  default:
    break;
  }
  if (!pool)
  {
    return keys;
  }
  keys.reserve(pool->size());
  for (const auto &pair : *pool)
  {
    keys.push_back(pair.first);
  }
  if (keys.empty())
  {
    keys.push_back("default");
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

void UWorldGenPaletteScreen::EnsureActiveSection()
{
  auto *pool = ActiveObjectPool();
  if (!pool)
  {
    return;
  }
  if (pool->empty())
  {
    (*pool)["default"] = WorldGenObjectSection{};
  }
  const auto keys = SectionKeysForPool();
  if (SectionTab >= static_cast<int>(keys.size()))
  {
    SectionTab = 0;
  }
}

void UWorldGenPaletteScreen::RebuildMainContent()
{
  if (!ContentScroll || !World || !Theme)
  {
    return;
  }
  RelayoutPanel();

  ContentSlots.clear();
  ContentSlotLabels.clear();
  ContentSlotHints.clear();
  ContentSlotKinds.clear();
  ContentSlotIds.clear();
  ContentSlotTerrainSlots.clear();
  UGuiPanel &content = ContentScroll->Content();
  content.ClearChildren();

  const int slotSize = Theme->HotbarSlotSize;

  if (ActiveTab == 0)
  {
    if (ObjectPoolTabs)
    {
      ObjectPoolTabs->SetVisible(true);
    }
    const auto keys = SectionKeysForPool();
    if (SectionTabs)
    {
      SectionTabs->SetTabs(keys);
      if (SectionTab >= static_cast<int>(keys.size()))
      {
        SectionTab = 0;
      }
      SectionTabs->SetActiveTab(SectionTab);
    }
    EnsureActiveSection();
    auto *pool = ActiveObjectPool();
    if (pool && SectionTab < static_cast<int>(keys.size()))
    {
      const std::string &sectionKey = keys[static_cast<size_t>(SectionTab)];
      auto it = pool->find(sectionKey);
      if (it != pool->end())
      {
        for (const auto &entry : it->second.Entries)
        {
          ContentKind kind = ContentKind::Object;
          std::string id = entry.ObjectName;
          if (entry.Mode == ObjectPlacementMode::ScatterBlocks)
          {
            kind = ContentKind::Block;
            id = entry.Scatter.BlockName;
          }
          if (id.empty())
          {
            continue;
          }
          const std::string label = DisplayNameForId(kind, id);
          const std::string hint = EntryHint(entry);
          auto slot = std::make_unique<UGuiSlot>(Theme);
          slot->SetBounds({0, 0, slotSize, slotSize});
          const GLuint tex = IconForEntry(kind, id);
          slot->SetIconTexture(tex);
          if (tex == 0)
          {
            slot->SetLabel(label);
          }
          slot->SetSelected(id == SelectedEntryId && kind == SelectedKind);
          const std::string entryId = id;
          const ContentKind entryKind = kind;
          slot->SetOnClick([this, entryKind, entryId, label]() {
            SelectEntry(entryKind, entryId, label);
          });
          UGuiSlot *ptr = static_cast<UGuiSlot *>(
              content.AddChild(std::move(slot)));
          ContentSlots.push_back(ptr);
          ContentSlotLabels.push_back(label);
          ContentSlotHints.push_back(hint);
          ContentSlotKinds.push_back(kind);
          ContentSlotIds.push_back(id);
          ContentSlotTerrainSlots.push_back("");
        }
      }
    }
    if (AddButton)
    {
      AddButton->SetLabel("+ Add object");
      AddButton->SetOnClick([this]() { OpenObjectPicker(); });
    }
  }
  else if (ActiveTab == 1)
  {
    if (ObjectPoolTabs)
    {
      ObjectPoolTabs->SetVisible(false);
    }
    if (SectionTabs)
    {
      SectionTabs->SetVisible(false);
    }
    const WorldGenSets &sets = World->GetWorldGenSets();
    for (const char *slotName : kTerrainSlots)
    {
      const std::string terrainSlot = slotName;
      const std::string blockIdCopy = StoredTerrainBlock(terrainSlot);
      const std::string iconBlockId = EffectiveTerrainBlock(terrainSlot);
      const std::string tooltip = TerrainSlotTooltip(terrainSlot);
      auto slot = std::make_unique<UGuiSlot>(Theme);
      slot->SetBounds({0, 0, slotSize, slotSize});
      if (!iconBlockId.empty())
      {
        slot->SetIconTexture(IconForEntry(ContentKind::Block, iconBlockId));
      }
      slot->SetSelected(SelectedTerrainSlot == terrainSlot);
      slot->SetOnClick([this, terrainSlot, blockIdCopy, tooltip]() {
        SelectEntry(ContentKind::Block, blockIdCopy, tooltip, terrainSlot);
      });
      UGuiSlot *ptr =
          static_cast<UGuiSlot *>(content.AddChild(std::move(slot)));
      ContentSlots.push_back(ptr);
      ContentSlotLabels.push_back(tooltip);
      ContentSlotHints.push_back("");
      ContentSlotKinds.push_back(ContentKind::Block);
      ContentSlotIds.push_back(blockIdCopy);
      ContentSlotTerrainSlots.push_back(terrainSlot);
    }
    if (AddButton)
    {
      AddButton->SetVisible(false);
    }
  }
  else
  {
    if (ObjectPoolTabs)
    {
      ObjectPoolTabs->SetVisible(false);
    }
    if (SectionTabs)
    {
      SectionTabs->SetVisible(false);
    }
    const WorldGenSets &sets = World->GetWorldGenSets();
    for (const char *slotName : kOreSlots)
    {
      const auto it = sets.Ores.find(slotName);
      const bool enabled = it == sets.Ores.end() ? true : it->second.Enabled;
      const std::string oreSlot = slotName;
      const std::string tooltip = OreSlotTooltip(oreSlot, enabled);
      auto slot = std::make_unique<UGuiSlot>(Theme);
      slot->SetBounds({0, 0, slotSize, slotSize});
      slot->SetIconTexture(IconForEntry(ContentKind::Block, oreSlot));
      slot->SetDimmed(!enabled);
      slot->SetSelected(SelectedOreSlot == oreSlot);
      slot->SetOnClick([this, oreSlot, tooltip]() {
        SelectEntry(ContentKind::Block, oreSlot, tooltip, "", oreSlot);
      });
      UGuiSlot *ptr =
          static_cast<UGuiSlot *>(content.AddChild(std::move(slot)));
      ContentSlots.push_back(ptr);
      ContentSlotLabels.push_back(tooltip);
      ContentSlotHints.push_back("");
      ContentSlotKinds.push_back(ContentKind::Block);
      ContentSlotIds.push_back(oreSlot);
      ContentSlotTerrainSlots.push_back("");
    }
    if (AddButton)
    {
      AddButton->SetVisible(false);
    }
  }

  if (ContentSlots.empty() && ActiveTab == 0)
  {
    auto hint = std::make_unique<UGuiLabel>(Theme, "No entries — click + Add object");
    content.AddChild(std::move(hint));
  }

  ContentScroll->SetScrollY(0);
  ContentScroll->SetAfterScrollLayout(
      [this](UGuiScrollView &) { LayoutContentIconGrid(); });
  ContentScroll->LayoutContent();
  LayoutContentIconGrid();
  ApplySlotSelection();
}

void UWorldGenPaletteScreen::AddPickerSlot(const CatalogEntry &entry,
                                             ContentKind kind)
{
  if (!PickerScroll || !Theme)
  {
    return;
  }
  const int slotSize = Theme->HotbarSlotSize;
  auto slot = std::make_unique<UGuiSlot>(Theme);
  slot->SetBounds({0, 0, slotSize, slotSize});
  const GLuint tex = IconForEntry(kind, entry.Id);
  slot->SetIconTexture(tex);
  if (tex == 0)
  {
    slot->SetLabel(entry.displayName);
  }
  const std::string id = entry.Id;
  slot->SetOnClick([this, id]() { OnPickerPick(id); });
  UGuiSlot *ptr = static_cast<UGuiSlot *>(
      PickerScroll->Content().AddChild(std::move(slot)));
  PickerSlots.push_back(ptr);
  PickerSlotLabels.push_back(entry.displayName);
}

void UWorldGenPaletteScreen::OpenObjectPicker()
{
  Picker = PickerMode::Object;
  if (PickerTitleLabel)
  {
    PickerTitleLabel->SetText("Pick object to add");
  }
  RebuildPicker();
}

void UWorldGenPaletteScreen::OpenTerrainPicker(const std::string &slotName)
{
  Picker = PickerMode::TerrainSlot;
  PickerTerrainSlot = slotName;
  if (PickerTitleLabel)
  {
    PickerTitleLabel->SetText("Pick block for " + slotName);
  }
  RebuildPicker();
}

void UWorldGenPaletteScreen::RebuildPicker()
{
  if (!PickerPanel || !PickerScroll || !Catalog || !Theme)
  {
    return;
  }
  PickerSlots.clear();
  PickerSlotLabels.clear();
  PickerScroll->Content().ClearChildren();

  const ContentKind kind =
      Picker == PickerMode::TerrainSlot ? ContentKind::Block : ContentKind::Object;
  for (const CatalogEntry &entry : CollectCatalogEntries(kind))
  {
    AddPickerSlot(entry, kind);
  }

  if (PickerSlots.empty())
  {
    auto hint = std::make_unique<UGuiLabel>(Theme, "No items in catalog");
    PickerScroll->Content().AddChild(std::move(hint));
  }

  PickerPanel->SetVisible(true);
  RelayoutPanel();
  PickerScroll->SetScrollY(0);
  PickerScroll->SetAfterScrollLayout(
      [this](UGuiScrollView &) { LayoutPickerIconGrid(); });
  PickerScroll->LayoutContent();
  LayoutPickerIconGrid();
}

void UWorldGenPaletteScreen::ClosePicker()
{
  Picker = PickerMode::None;
  PickerTerrainSlot.clear();
  PickerSlots.clear();
  PickerSlotLabels.clear();
  if (PickerScroll)
  {
    PickerScroll->Content().ClearChildren();
  }
  if (PickerPanel)
  {
    PickerPanel->SetVisible(false);
    PickerPanel->SetBounds({0, 0, 0, 0});
  }
}

void UWorldGenPaletteScreen::OnPickerPick(const std::string &id)
{
  if (!World || id.empty())
  {
    ClosePicker();
    return;
  }
  const bool terrainPick = Picker == PickerMode::TerrainSlot;
  const std::string terrainSlot = PickerTerrainSlot;
  if (Picker == PickerMode::Object)
  {
    EnsureActiveSection();
    auto *pool = ActiveObjectPool();
    const auto keys = SectionKeysForPool();
    if (pool && SectionTab < static_cast<int>(keys.size()))
    {
      WorldGenObjectEntry entry;
      entry.ObjectName = id;
      entry.Weight = 1;
      entry.Spacing = 32;
      (*pool)[keys[static_cast<size_t>(SectionTab)]].Entries.push_back(entry);
      SaveSets();
    }
  }
  else if (terrainPick && !terrainSlot.empty())
  {
    World->GetWorldGenSets().Terrain[terrainSlot].Block = id;
    SaveSets();
  }
  ClosePicker();
  ContentDirty = true;
  if (terrainPick && !terrainSlot.empty())
  {
    SelectEntry(ContentKind::Block, id,
                DisplayNameForId(ContentKind::Block, id), terrainSlot);
  }
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
