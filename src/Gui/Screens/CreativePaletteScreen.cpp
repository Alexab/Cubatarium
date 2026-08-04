#include "Gui/Screens/CreativePaletteScreen.h"
#include "Content/ContentTypeRegistry.h"
#include "Game/GameSession.h"
#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Interfaces/IUContentCatalog.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Layout/DockedOverlayLayout.h"
#include "Gui/Layout/GuiTooltipLayout.h"
#include "Gui/Preview/ContentPreviewDock.h"
#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiTabBar.h"

#include "ResourcePacks/BlockNameUtil.h"

#include <algorithm>

namespace cutum
{

UCreativePaletteScreen::UCreativePaletteScreen(IUContentCatalog *catalog,
                                               UGameSession *session,
                                               IUGuiIconSource *icons,
                                               UContentPreviewRenderer *previewRenderer)
    : Catalog(catalog), Session(session), Icons(icons),
      PreviewRenderer(previewRenderer)
{
}

UCreativePaletteScreen::~UCreativePaletteScreen() = default;

bool UCreativePaletteScreen::PickHotbarStrip(int x, int y,
                                             SlotAddress &out) const
{
  if (!Visible)
  {
    return false;
  }
  for (size_t i = 0; i < HotbarStripSlots.size(); ++i)
  {
    const UGuiSlot *slot = HotbarStripSlots[i];
    if (!slot || !slot->IsVisible() || !slot->GetBounds().Contains(x, y))
    {
      continue;
    }
    out = SlotAddress{};
    out.surface = SlotSurface::Hotbar;
    out.bar = 0;
    out.slot = i;
    return true;
  }
  return false;
}

bool UCreativePaletteScreen::PickGridSlot(int x, int y, SlotAddress &out) const
{
  if (!Visible)
  {
    return false;
  }
  for (size_t i = 0; i < GridSlots.size(); ++i)
  {
    const UGuiSlot *slot = GridSlots[i];
    if (!slot || !slot->IsVisible() || !slot->GetBounds().Contains(x, y))
    {
      continue;
    }
    if (i >= GridEntryIds.size())
    {
      return false;
    }
    out = SlotAddress{};
    out.surface = SlotSurface::PaletteGrid;
    out.paletteKind = Kind;
    out.entryId = GridEntryIds[i];
    out.bar = 0;
    out.slot = 0;
    return true;
  }
  return false;
}

bool UCreativePaletteScreen::PickSlot(int x, int y, SlotAddress &out) const
{
  if (PickHotbarStrip(x, y, out))
  {
    return true;
  }
  return PickGridSlot(x, y, out);
}

void UCreativePaletteScreen::SetVisible(bool visible)
{
  Visible = visible;
  if (Root)
  {
    Root->SetVisible(visible);
  }
  if (!visible)
  {
    SelectedEntryId.clear();
    if (PreviewDock)
    {
      PreviewDock->ClearSelection();
    }
  }
}

void UCreativePaletteScreen::Toggle() { SetVisible(!Visible); }

void UCreativePaletteScreen::ApplyMainTab(int tab)
{
  switch (tab)
  {
  case 0:
    Kind = ContentKind::Block;
    break;
  case 1:
    Kind = ContentKind::Object;
    break;
  case 2:
    Kind = ContentKind::UCreature;
    break;
  case 3:
    Kind = ContentKind::Skin;
    break;
  default:
    Kind = ContentKind::Item;
    break;
  }
  if (MainTabs)
  {
    MainTabs->SetActiveTab(tab);
  }
  if (Catalog)
  {
    const auto Types = Catalog->GetTypeIds(Kind);
    ActiveTypeId = Types.empty() ? "misc" : Types.front();
  }
  SelectedEntryId.clear();
  if (PreviewDock)
  {
    PreviewDock->ClearSelection();
  }
  Built = false;
  RelayoutPanel();
}

void UCreativePaletteScreen::OpenWithMainTab(int tab)
{
  ApplyMainTab(tab);
  SetVisible(true);
}

int UCreativePaletteScreen::GetActiveMainTab() const
{
  if (MainTabs)
  {
    return MainTabs->GetActiveTab();
  }
  switch (Kind)
  {
  case ContentKind::Block:
    return 0;
  case ContentKind::Object:
    return 1;
  case ContentKind::UCreature:
    return 2;
  case ContentKind::Skin:
    return 3;
  case ContentKind::Item:
  default:
    return 4;
  }
}

void UCreativePaletteScreen::Build(UGuiContext &ctx)
{
  Theme = &ctx.GetTheme();
  Renderer = &ctx.GetRenderer();
  HotbarStripBuilt = false;
  HotbarStripSlots.clear();
  HotbarStripLabel = nullptr;

  auto rootShell = std::make_unique<UGuiPanel>(Theme);
  rootShell->SetDrawBackground(false);
  rootShell->SetClipChildren(false);
  RootShell = rootShell.get();

  auto panel = std::make_unique<UGuiPanel>(Theme);
  panel->SetDrawBackground(true);
  panel->SetZOrder(10);
  Panel = panel.get();

  auto mainTabs = std::make_unique<UGuiTabBar>(Theme);
  mainTabs->SetTabs({"Blocks", "Objects", "Creatures", "Skins", "Tools"});
  MainTabs = mainTabs.get();
  mainTabs->SetOnTabChanged(
      [this](int tab) { ApplyMainTab(tab); });

  auto subTabs = std::make_unique<UGuiTabBar>(Theme);
  SubTabs = subTabs.get();
  subTabs->SetOnTabChanged(
      [this](int tab)
      {
        if (Catalog)
        {
          const auto Types = Catalog->GetTypeIds(Kind);
          if (tab >= 0 && tab < static_cast<int>(Types.size()))
          {
            ActiveTypeId = Types[static_cast<size_t>(tab)];
          }
        }
        SelectedEntryId.clear();
        if (PreviewDock)
        {
          PreviewDock->ClearSelection();
        }
        Built = false;
      });

  auto scroll = std::make_unique<UGuiScrollView>(Theme);
  Scroll = scroll.get();

  auto tooltip = std::make_unique<UGuiLabel>(Theme, "");
  tooltip->SetVisible(false);
  TooltipLabel = tooltip.get();
  panel->AddChild(std::move(tooltip));

  auto usageHint = std::make_unique<UGuiLabel>(
      Theme,
      "Click → selected slot · Drag onto Hotbar row below · keys 1-0 = hand");
  usageHint->SetUseSecondaryColor(true);
  usageHint->SetVisible(false);
  UsageHintLabel = usageHint.get();
  panel->AddChild(std::move(usageHint));

  auto stripLabel = std::make_unique<UGuiLabel>(Theme, "Hotbar (drop tools here)");
  stripLabel->SetUseSecondaryColor(true);
  HotbarStripLabel = stripLabel.get();
  rootShell->AddChild(std::move(stripLabel));

  panel->AddChild(std::move(mainTabs));
  panel->AddChild(std::move(subTabs));
  panel->AddChild(std::move(scroll));
  rootShell->AddChild(std::move(panel));

  PreviewDock =
      std::make_unique<UContentPreviewDock>(Theme, PreviewRenderer);
  rootShell->AddChild(PreviewDock->ReleasePanel());

  Root = std::move(rootShell);
  Kind = ContentKind::Block;
  ActiveTypeId = "misc";
  Built = false;
  EnsureHotbarStrip();
}

void UCreativePaletteScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  RelayoutPanel();
}

void UCreativePaletteScreen::RelayoutPanel()
{
  if (!Panel || !Theme)
  {
    return;
  }
  DockedLayout layout = DockedOverlayLayout::Compute(
      ViewportW, ViewportH, GetContentOffsetX(), GetContentOffsetY(), 70, 28,
      *Theme);
  const int slotSize = Theme->HotbarSlotSize;
  const int hotbarReserve =
      slotSize + Theme->HotbarMarginBottom + Theme->Padding * 2;
  const int maxPanelH =
      std::max(Scaled(120), ViewportH - hotbarReserve - ViewportH / 12);
  if (layout.main.H > maxPanelH)
  {
    const int shrink = layout.main.H - maxPanelH;
    layout.main.H = maxPanelH;
    layout.main.Y += shrink / 2;
    layout.preview.H = maxPanelH;
    layout.preview.Y = layout.main.Y;
  }
  const int maxBottom =
      GetContentOffsetY() + ViewportH - hotbarReserve;
  if (layout.main.Y + layout.main.H > maxBottom)
  {
    layout.main.Y = maxBottom - layout.main.H;
    layout.preview.Y = layout.main.Y;
  }
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
  if (MainTabs)
  {
    MainTabs->SetBounds(
        {panelX + pad, panelY + pad, panelW - pad * 2, tabH});
  }
  if (SubTabs)
  {
    SubTabs->SetBounds({panelX + pad, panelY + pad + tabH + pad, panelW - pad * 2,
                        tabH});
  }
  const int hintH = Theme->FontSizeBody + 4;
  const bool showToolsHint = Kind == ContentKind::Item;
  if (UsageHintLabel)
  {
    UsageHintLabel->SetVisible(showToolsHint);
    if (showToolsHint)
    {
      UsageHintLabel->SetBounds(
          {panelX + pad, panelY + pad + tabH + pad + tabH + pad / 2,
           panelW - pad * 2, hintH});
    }
  }
  if (Scroll)
  {
    const int hintGap = showToolsHint ? (hintH + pad / 2) : 0;
    const int scrollTop =
        panelY + pad + tabH + pad + tabH + pad + hintGap;
    const int scrollH = std::max(0, panelH - (scrollTop - panelY) - pad);
    Scroll->SetBounds({panelX + pad, scrollTop, panelW - pad * 2, scrollH});
    if (Built)
    {
      Scroll->LayoutContent();
      LayoutGridInScroll();
    }
  }
  LayoutHotbarStrip();
}

void UCreativePaletteScreen::EnsureHotbarStrip()
{
  if (HotbarStripBuilt || !RootShell || !Theme || !Session)
  {
    return;
  }
  for (size_t i = 0; i < 10; ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(Theme);
    const size_t index = i;
    SlotAddress address;
    address.surface = SlotSurface::Hotbar;
    address.bar = 0;
    address.slot = index;
    slot->SetOnClick(
        [this, index]()
        {
          if (!Session)
          {
            return;
          }
          if (!Session->ApplyPendingAssignment(0, index))
          {
            Session->SelectSlot(0, index);
          }
        });
    slot->SetOnBeginDrag(
        [this, address]()
        {
          if (!Session)
          {
            return;
          }
          const InventoryEntryRef entry =
              Session->GetHotbarEntryRef(address.bar, address.slot);
          if (!entry.empty)
          {
            Session->BeginDragFromSlot(address, entry);
          }
        });
    const int hotkeyNumber = (index < 9) ? static_cast<int>(index + 1) : 0;
    slot->SetCornerHint(std::to_string(hotkeyNumber));
    HotbarStripSlots.push_back(
        static_cast<UGuiSlot *>(RootShell->AddChild(std::move(slot))));
    HotbarStripSlots.back()->SetZOrder(50);
  }
  if (HotbarStripLabel)
  {
    HotbarStripLabel->SetZOrder(49);
  }
  HotbarStripBuilt = true;
  LayoutHotbarStrip();
}

void UCreativePaletteScreen::LayoutHotbarStrip()
{
  if (!HotbarStripBuilt || !Theme)
  {
    return;
  }
  EnsureHotbarStrip();
  const int slotSize = Theme->HotbarSlotSize;
  const int gap = Theme->HotbarSlotGap;
  const int totalW = static_cast<int>(HotbarStripSlots.size()) * slotSize +
                     (static_cast<int>(HotbarStripSlots.size()) - 1) * gap;
  const int startX = GetContentOffsetX() + (ViewportW - totalW) / 2;
  const int rowY = GetContentOffsetY() + ViewportH - Theme->HotbarMarginBottom -
                   slotSize;
  if (HotbarStripLabel)
  {
    HotbarStripLabel->SetBounds(
        {startX, rowY - Theme->FontSizeBody - Theme->Padding / 2, totalW,
         Theme->FontSizeBody + 4});
  }
  int x = startX;
  for (UGuiSlot *slot : HotbarStripSlots)
  {
    if (slot)
    {
      slot->SetBounds({x, rowY, slotSize, slotSize});
      slot->SetVisible(true);
      x += slotSize + gap;
    }
  }
}

void UCreativePaletteScreen::SyncHotbarStrip()
{
  if (!Session || !HotbarStripBuilt)
  {
    return;
  }
  const auto primary = Session->GetBarSlots(0);
  for (size_t i = 0; i < HotbarStripSlots.size() && i < primary.size(); ++i)
  {
    UGuiSlot *slot = HotbarStripSlots[i];
    if (!slot)
    {
      continue;
    }
    slot->SetSelected(primary[i].selected);
    slot->SetLabel(primary[i].label);
    GLuint tex = 0;
    if (Icons && !primary[i].Id.empty())
    {
      switch (primary[i].entryKind)
      {
      case InventoryEntryKind::Block:
        tex = Icons->GetBlockIconTexture(primary[i].Id);
        break;
      case InventoryEntryKind::Object:
        tex = Icons->GetObjectIconTexture(primary[i].Id);
        break;
      case InventoryEntryKind::UCreature:
        tex = Icons->GetCreatureIconTexture(primary[i].Id);
        break;
      case InventoryEntryKind::Skin:
        tex = Icons->GetSkinIconTexture(primary[i].Id);
        break;
      case InventoryEntryKind::Item:
        tex = Icons->GetItemIconTexture(primary[i].Id);
        break;
      }
    }
    slot->SetIconTexture(tex);
    slot->SetWearProgress(primary[i].entryKind == InventoryEntryKind::Item
                              ? primary[i].wear
                              : 0.f);
    slot->SetBroken(primary[i].entryKind == InventoryEntryKind::Item &&
                    primary[i].broken);
  }
}

void UCreativePaletteScreen::SetPointerPosition(int x, int y)
{
  PointerX = x;
  PointerY = y;
}

void UCreativePaletteScreen::SetPointerPressed(bool pressed)
{
  if (!pressed)
  {
    HoldTimer = 0.0;
    HoldSlotIndex = -1;
  }
  PointerPressed = pressed;
}

void UCreativePaletteScreen::UpdateTooltip()
{
  if (!TooltipLabel || !Theme)
  {
    return;
  }
  std::string text;
  int tipX = PointerX;
  int tipY = PointerY;
  auto labelAt = [this](size_t index) -> std::string {
    if (index < GridEntryLabels.size() && !GridEntryLabels[index].empty())
    {
      return GridEntryLabels[index];
    }
    if (index < GridEntryIds.size())
    {
      return HumanizeBlockName(GridEntryIds[index]);
    }
    return {};
  };

  if (HoldTimer >= kHoldTooltipSeconds && HoldSlotIndex >= 0 &&
      static_cast<size_t>(HoldSlotIndex) < GridEntryIds.size())
  {
    text = labelAt(static_cast<size_t>(HoldSlotIndex));
    if (Kind == ContentKind::UCreature &&
        static_cast<size_t>(HoldSlotIndex) < GridSpawnHints.size() &&
        !GridSpawnHints[static_cast<size_t>(HoldSlotIndex)].empty())
    {
      text += "\n" + GridSpawnHints[static_cast<size_t>(HoldSlotIndex)];
    }
    if (PointerX < 0 || PointerY < 0)
    {
      UGuiSlot *slot = static_cast<size_t>(HoldSlotIndex) < GridSlots.size()
                           ? GridSlots[static_cast<size_t>(HoldSlotIndex)]
                           : nullptr;
      if (slot)
      {
        const GuiRect b = slot->GetBounds();
        tipX = b.X + b.W / 2;
        tipY = b.Y + b.H / 2;
      }
    }
  }
  else if (PointerX >= 0 && PointerY >= 0 && Scroll)
  {
    for (size_t i = 0; i < GridSlots.size(); ++i)
    {
      UGuiSlot *slot = GridSlots[i];
      if (slot && slot->IsVisible() &&
          slot->GetBounds().Contains(PointerX, PointerY) &&
          i < GridEntryIds.size())
      {
        text = labelAt(i);
        if (Kind == ContentKind::UCreature &&
            static_cast<size_t>(i) < GridSpawnHints.size() &&
            !GridSpawnHints[i].empty())
        {
          text += "\n" + GridSpawnHints[i];
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

void UCreativePaletteScreen::Update(double dt)
{
  if (!Visible || !Panel || !Catalog || !Theme)
  {
    return;
  }
  RelayoutPanel();
  SyncHotbarStrip();

  if (PointerPressed && PointerX >= 0 && PointerY >= 0)
  {
    int slotUnderPointer = -1;
    for (size_t i = 0; i < GridSlots.size(); ++i)
    {
      UGuiSlot *slot = GridSlots[i];
      if (slot && slot->IsVisible() &&
          slot->GetBounds().Contains(PointerX, PointerY))
      {
        slotUnderPointer = static_cast<int>(i);
        break;
      }
    }
    if (slotUnderPointer >= 0)
    {
      if (slotUnderPointer != HoldSlotIndex)
      {
        HoldSlotIndex = slotUnderPointer;
        HoldTimer = 0.0;
      }
      else
      {
        HoldTimer += dt;
      }
    }
    else
    {
      HoldSlotIndex = -1;
      HoldTimer = 0.0;
    }
  }

  if (SubTabs)
  {
    const auto Types = Catalog->GetTypeIds(Kind);
    std::vector<std::string> labels;
    for (const auto &Id : Types)
    {
      labels.push_back(Catalog->GetTypeDisplayName(Id));
    }
    if (labels.empty())
    {
      labels.push_back("Misc");
    }
    SubTabs->SetTabs(labels);
    if (!Types.empty())
    {
      auto it = std::find(Types.begin(), Types.end(), ActiveTypeId);
      if (it == Types.end())
      {
        ActiveTypeId = Types.front();
        SubTabs->SetActiveTab(0);
      }
      else
      {
        SubTabs->SetActiveTab(
            static_cast<int>(std::distance(Types.begin(), it)));
      }
    }
    else
    {
      ActiveTypeId.clear();
      SubTabs->SetActiveTab(0);
    }
  }

  if (!Built)
  {
    RebuildGrid();
  }
  if (PreviewDock)
  {
    PreviewDock->Update(dt);
  }
  UpdateTooltip();
}

void UCreativePaletteScreen::SyncPreviewDock()
{
  if (!PreviewDock)
  {
    return;
  }
  PreviewDock->SetOnChange(nullptr);
  if (SelectedEntryId.empty() || !PreviewRenderer ||
      !PreviewRenderer->SupportsKind(Kind))
  {
    PreviewDock->ClearSelection();
    return;
  }
  std::string label = SelectedEntryId;
  for (size_t i = 0; i < GridEntryIds.size(); ++i)
  {
    if (GridEntryIds[i] == SelectedEntryId &&
        i < GridEntryLabels.size())
    {
      label = GridEntryLabels[i];
      break;
    }
  }
  PreviewDock->SetSelection(Kind, SelectedEntryId, label);
}

void UCreativePaletteScreen::RenderPreview()
{
  if (PreviewDock)
  {
    PreviewDock->RenderIfDirty();
  }
}

void UCreativePaletteScreen::RebuildGrid()
{
  if (!Scroll || !Catalog || !Session || !Theme)
  {
    return;
  }
  Scroll->Content().ClearChildren();
  GridSlots.clear();
  GridEntryIds.clear();
  GridEntryLabels.clear();
  GridSpawnHints.clear();

  const auto entries =
      Catalog->GetEntries(Kind, ActiveTypeId.empty() ? "misc" : ActiveTypeId);
  const int slotSize = Theme->HotbarSlotSize;
  for (size_t i = 0; i < entries.size(); ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(Theme);
    slot->SetBounds({0, 0, slotSize, slotSize});
    const std::string entryId = entries[i].Id;
    slot->SetSelected(entryId == SelectedEntryId);
    if (Icons)
    {
      GLuint tex = 0;
      if (Kind == ContentKind::Block)
      {
        tex = Icons->GetBlockIconTexture(entryId);
      }
      else if (Kind == ContentKind::Object)
      {
        tex = Icons->GetObjectIconTexture(entryId);
      }
      else if (Kind == ContentKind::UCreature)
      {
        tex = Icons->GetCreatureIconTexture(entryId);
      }
      else if (Kind == ContentKind::Skin)
      {
        tex = Icons->GetSkinIconTexture(entryId);
      }
      else if (Kind == ContentKind::Item)
      {
        tex = Icons->GetItemIconTexture(entryId);
      }
      slot->SetIconTexture(tex);
    }
    InventoryEntryRef entry;
    entry.empty = false;
    entry.Id = entryId;
    entry.count = 1;
    switch (Kind)
    {
    case ContentKind::Block:
      entry.kind = InventoryEntryKind::Block;
      break;
    case ContentKind::Object:
      entry.kind = InventoryEntryKind::Object;
      break;
    case ContentKind::UCreature:
      entry.kind = InventoryEntryKind::UCreature;
      break;
    case ContentKind::Skin:
      entry.kind = InventoryEntryKind::Skin;
      break;
    case ContentKind::Item:
      entry.kind = InventoryEntryKind::Item;
      entry.wear = 0.f;
      entry.broken = false;
      break;
    }

    SlotAddress address;
    address.surface = SlotSurface::PaletteGrid;
    address.paletteKind = Kind;
    address.entryId = entryId;

    slot->SetOnClick(
        [this, entry]()
        {
          if (!Session)
          {
            return;
          }
          SelectedEntryId = entry.Id;
          SyncPreviewDock();
          const size_t bar = 0;
          const size_t slot = Session->GetSelectedSlot(bar);
          if (Session->AssignToHotbar(entry, bar, slot))
          {
            Session->ClearPendingAssignment();
            Session->SelectSlot(bar, slot);
          }
          else
          {
            Session->BeginPendingAssignment(entry);
          }
          Built = false;
        });
    slot->SetOnBeginDrag(
        [this, address, entry]()
        {
          if (Session)
          {
            Session->BeginDragFromSlot(address, entry);
          }
        });
    std::string spawnHint;
    if (Kind == ContentKind::UCreature && Session)
    {
      const bool canSpawn = Session->CanSpawnCreatureByView(entryId);
      slot->SetDimmed(!canSpawn);
      if (!canSpawn)
      {
        spawnHint = Session->GetCreatureSpawnBlockedHint(entryId);
      }
    }
    UGuiSlot *ptr =
        static_cast<UGuiSlot *>(Scroll->Content().AddChild(std::move(slot)));
    GridSlots.push_back(ptr);
    GridEntryIds.push_back(entryId);
    GridEntryLabels.push_back(entries[i].displayName);
    GridSpawnHints.push_back(std::move(spawnHint));
  }

  Scroll->SetAfterScrollLayout([this](UGuiScrollView &)
                               { LayoutGridInScroll(); });
  Scroll->LayoutContent();
  LayoutGridInScroll();
  SyncPreviewDock();
  Built = true;
}

void UCreativePaletteScreen::LayoutGridInScroll()
{
  if (!Scroll || !Theme)
  {
    return;
  }
  const GuiRect contentRect = Scroll->Content().GetBounds();
  const int slotSize = Theme->HotbarSlotSize;
  const int gap = Theme->HotbarSlotGap;
  const int viewportW = contentRect.W;
  const int viewportH = std::max(1, Scroll->GetBounds().H);
  const int denom = std::max(1, slotSize + gap);
  const int cols = std::max(1, (viewportW + gap) / denom);
  const int contentTop = contentRect.Y;

  int x = contentRect.X;
  int y = contentTop;
  for (size_t i = 0; i < GridSlots.size(); ++i)
  {
    UGuiSlot *slot = GridSlots[i];
    if (!slot)
    {
      continue;
    }
    slot->SetBounds({x, y, slotSize, slotSize});
    if ((i + 1) % static_cast<size_t>(cols) == 0)
    {
      x = contentRect.X;
      y += slotSize + gap;
    }
    else
    {
      x += slotSize + gap;
    }
  }

  const int rows = GridSlots.empty()
                       ? 0
                       : static_cast<int>((GridSlots.size() + cols - 1) / cols);
  const int contentHeight =
      std::max(viewportH, rows * (slotSize + gap) - (rows > 0 ? gap : 0) + 8);
  Scroll->Content().SetBounds(
      {contentRect.X, contentTop, viewportW, contentHeight});
}

} // namespace cutum
