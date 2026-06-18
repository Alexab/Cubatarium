#include "Gui/Screens/CreativePaletteScreen.h"
#include "Game/GameSession.h"
#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include "Gui/Interfaces/IGuiIconSource.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiTabBar.h"

#include "ResourcePacks/BlockNameUtil.h"

namespace cutum
{

UCreativePaletteScreen::UCreativePaletteScreen(IContentCatalog *catalog,
                                               UGameSession *session,
                                               IGuiIconSource *icons)
    : Catalog(catalog), Session(session), Icons(icons)
{
}

bool UCreativePaletteScreen::PickSlot(int x, int y, SlotAddress &out) const
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
    out.surface = SlotSurface::PaletteGrid;
    out.paletteKind = Kind;
    out.entryId = GridEntryIds[i];
    out.bar = 0;
    out.slot = 0;
    return true;
  }
  return false;
}

void UCreativePaletteScreen::SetVisible(bool visible)
{
  Visible = visible;
  if (Root)
  {
    Root->SetVisible(visible);
  }
}

void UCreativePaletteScreen::Toggle() { SetVisible(!Visible); }

void UCreativePaletteScreen::Build(UGuiContext &ctx)
{
  Theme = &ctx.GetTheme();
  auto panel = std::make_unique<UGuiPanel>(Theme);
  panel->SetVisible(false);
  Panel = panel.get();

  auto mainTabs = std::make_unique<UGuiTabBar>(Theme);
  mainTabs->SetTabs({"Blocks", "Objects", "Creatures", "Skins"});
  MainTabs = mainTabs.get();
  mainTabs->SetOnTabChanged(
      [this](int tab)
      {
        switch (tab)
        {
        case 0:
          Kind = ContentKind::Block;
          break;
        case 1:
          Kind = ContentKind::UObject;
          break;
        case 2:
          Kind = ContentKind::UCreature;
          break;
        default:
          Kind = ContentKind::Skin;
          break;
        }
        if (Catalog)
        {
          const auto Types = Catalog->GetTypeIds(Kind);
          ActiveTypeId = Types.empty() ? "misc" : Types.front();
        }
        SelectedEntryId.clear();
        Built = false;
      });

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
        Built = false;
      });

  auto scroll = std::make_unique<UGuiScrollView>(Theme);
  Scroll = scroll.get();

  auto tooltip = std::make_unique<UGuiLabel>(Theme, "");
  tooltip->SetVisible(false);
  TooltipLabel = tooltip.get();
  panel->AddChild(std::move(tooltip));

  panel->AddChild(std::move(mainTabs));
  panel->AddChild(std::move(subTabs));
  panel->AddChild(std::move(scroll));
  Root = std::move(panel);
  Kind = ContentKind::Block;
  ActiveTypeId = "misc";
  Built = false;
}

void UCreativePaletteScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  RelayoutPanel();
}

void UCreativePaletteScreen::RelayoutPanel()
{
  if (!Panel)
  {
    return;
  }
  const int panelW = ViewportW * 60 / 100;
  const int panelH = ViewportH * 70 / 100;
  const int panelX = (ViewportW - panelW) / 2;
  const int panelY = (ViewportH - panelH) / 2;
  Panel->SetBounds({panelX, panelY, panelW, panelH});

  if (MainTabs)
  {
    MainTabs->SetBounds({panelX + 8, panelY + 8, panelW - 16, 28});
  }
  if (SubTabs)
  {
    SubTabs->SetBounds({panelX + 8, panelY + 40, panelW - 16, 28});
  }
  if (TooltipLabel && Panel)
  {
    const GuiRect b = Panel->GetBounds();
    TooltipLabel->SetBounds({b.X + 8, b.Y + b.H - 28, b.W - 16, 22});
  }
  if (Scroll)
  {
    const int scrollH = std::max(0, panelH - 84);
    Scroll->SetBounds({panelX + 8, panelY + 76, panelW - 16, scrollH});
    if (Built)
    {
      Scroll->LayoutContent();
      LayoutGridInScroll();
    }
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
  if (!TooltipLabel)
  {
    return;
  }
  std::string text;
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
  }
  else if (PointerX >= 0 && PointerY >= 0 && Scroll)
  {
    for (size_t i = 0; i < GridSlots.size(); ++i)
    {
      UGuiSlot *slot = GridSlots[i];
      if (slot && slot->IsVisible() && slot->GetBounds().Contains(PointerX, PointerY) &&
          i < GridEntryIds.size())
      {
        text = labelAt(i);
        break;
      }
    }
  }
  TooltipLabel->SetText(text);
  TooltipLabel->SetVisible(!text.empty());
}

void UCreativePaletteScreen::Update(double dt)
{
  if (!Visible || !Panel || !Catalog || !Theme)
  {
    return;
  }
  RelayoutPanel();

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
  UpdateTooltip();
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

  const auto entries =
      Catalog->GetEntries(Kind, ActiveTypeId.empty() ? "misc" : ActiveTypeId);
  const int slotSize = Theme->HotbarSlotSize;
  for (size_t i = 0; i < entries.size(); ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(Theme, slotSize);
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
      else if (Kind == ContentKind::UObject)
      {
        tex = Icons->GetPrefabIconTexture(entryId);
      }
      else if (Kind == ContentKind::UCreature)
      {
        tex = Icons->GetCreatureIconTexture(entryId);
      }
      else if (Kind == ContentKind::Skin)
      {
        tex = Icons->GetSkinIconTexture(entryId);
      }
      slot->SetIconTexture(tex);
    }
    InventoryEntryRef entry;
    entry.empty = false;
    entry.Id = entryId;
    switch (Kind)
    {
    case ContentKind::Block:
      entry.kind = InventoryEntryKind::Block;
      break;
    case ContentKind::UObject:
      entry.kind = InventoryEntryKind::UObject;
      break;
    case ContentKind::UCreature:
      entry.kind = InventoryEntryKind::UCreature;
      break;
    case ContentKind::Skin:
      entry.kind = InventoryEntryKind::Skin;
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
          Session->BeginPendingAssignment(entry);
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
    UGuiSlot *ptr =
        static_cast<UGuiSlot *>(Scroll->Content().AddChild(std::move(slot)));
    GridSlots.push_back(ptr);
    GridEntryIds.push_back(entryId);
    GridEntryLabels.push_back(entries[i].displayName);
  }

  Scroll->SetAfterScrollLayout([this](UGuiScrollView &)
                               { LayoutGridInScroll(); });
  Scroll->LayoutContent();
  LayoutGridInScroll();
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
