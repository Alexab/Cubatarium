#include "Gui/Screens/SurvivalInventoryScreen.h"

#include "Game/GameSession.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Layout/DockedOverlayLayout.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiTabBar.h"
#include "Game/Inventory/SlotInteraction.h"

#include <algorithm>
#include <memory>

namespace cutum
{

USurvivalInventoryScreen::USurvivalInventoryScreen(IUContentCatalog *catalog,
                                                     UGameSession *session,
                                                     IUGuiIconSource *icons)
    : Catalog(catalog), Session(session), Icons(icons)
{
}

USurvivalInventoryScreen::~USurvivalInventoryScreen() = default;

void USurvivalInventoryScreen::Build(UGuiContext &ctx)
{
  Theme = &ctx.GetTheme();
  Built = false;
  Kind = ContentKind::Block;

  auto rootShell = std::make_unique<UGuiPanel>(Theme);
  rootShell->SetDrawBackground(false);
  rootShell->SetClipChildren(false);
  RootShell = rootShell.get();

  auto panel = std::make_unique<UGuiPanel>(Theme);
  panel->SetDrawBackground(true);
  panel->SetZOrder(10);
  Panel = panel.get();

  auto title = std::make_unique<UGuiLabel>(Theme, "Backpack");
  title->SetUseSecondaryColor(true);
  Title = title.get();
  Panel->AddChild(std::move(title));

  auto tabs = std::make_unique<UGuiTabBar>(Theme);
  tabs->SetTabs({"Blocks", "Items"});
  Tabs = tabs.get();
  Tabs->SetOnTabChanged([this](int tab) { ApplyTab(tab); });
  Panel->AddChild(std::move(tabs));

  auto empty = std::make_unique<UGuiLabel>(
      Theme, "Empty — break blocks or craft to collect resources");
  empty->SetUseSecondaryColor(true);
  EmptyHint = empty.get();
  Panel->AddChild(std::move(empty));

  auto scroll = std::make_unique<UGuiScrollView>(Theme);
  Scroll = scroll.get();
  Panel->AddChild(std::move(scroll));

  RootShell->AddChild(std::move(panel));
  Root = std::move(rootShell);

  SetVisible(false);
}

void USurvivalInventoryScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  RelayoutPanel();
}

void USurvivalInventoryScreen::SetVisible(bool visible)
{
  Visible = visible;
  if (RootShell)
  {
    RootShell->SetVisible(visible);
  }
  if (visible)
  {
    RebuildGrid();
    RelayoutPanel();
  }
}

void USurvivalInventoryScreen::Toggle() { SetVisible(!Visible); }

void USurvivalInventoryScreen::Update(double /*dt*/) {}

void USurvivalInventoryScreen::ApplyTab(int tab)
{
  Kind = (tab == 1) ? ContentKind::Item : ContentKind::Block;
  RebuildGrid();
  RelayoutPanel();
}

void USurvivalInventoryScreen::RelayoutPanel()
{
  if (!Panel || !Theme || !RootShell)
  {
    return;
  }

  DockedLayout layout = DockedOverlayLayout::Compute(
      ViewportW, ViewportH, GetContentOffsetX(), GetContentOffsetY(), 70, 0,
      *Theme);
  const int slotSize = Theme->HotbarSlotSize;
  const int hotbarReserve =
      slotSize + Theme->HotbarMarginBottom + Theme->Padding * 2;
  const int maxPanelH =
      std::max(Scaled(160), ViewportH - hotbarReserve - ViewportH / 12);
  if (layout.main.H > maxPanelH)
  {
    const int shrink = layout.main.H - maxPanelH;
    layout.main.H = maxPanelH;
    layout.main.Y += shrink / 2;
  }
  const int maxBottom = GetContentOffsetY() + ViewportH - hotbarReserve;
  if (layout.main.Y + layout.main.H > maxBottom)
  {
    layout.main.Y = maxBottom - layout.main.H;
  }

  // Survival backpack uses the main dock only (no preview pane).
  layout.main.W = std::min(layout.main.W + layout.preview.W + Theme->Padding,
                           ViewportW - Theme->Padding * 2);
  layout.main.X = GetContentOffsetX() +
                  (ViewportW - layout.main.W) / 2;

  RootShell->SetBounds(
      {GetContentOffsetX(), GetContentOffsetY(), ViewportW, ViewportH});
  Panel->SetBounds(layout.main);

  const int pad = Theme->Padding;
  const int panelX = layout.main.X;
  const int panelY = layout.main.Y;
  const int panelW = layout.main.W;
  const int panelH = layout.main.H;
  const int tabH = Theme->TabBarHeight;
  const int titleH = Theme->FontSizeBody + 8;

  if (Title)
  {
    Title->SetBounds({panelX + pad, panelY + pad, panelW - pad * 2, titleH});
  }
  if (Tabs)
  {
    Tabs->SetBounds(
        {panelX + pad, panelY + pad + titleH + pad / 2, panelW - pad * 2, tabH});
  }

  const int scrollTop = panelY + pad + titleH + pad / 2 + tabH + pad;
  const int scrollH = std::max(0, panelH - (scrollTop - panelY) - pad);
  if (Scroll)
  {
    Scroll->SetBounds({panelX + pad, scrollTop, panelW - pad * 2, scrollH});
  }
  if (EmptyHint)
  {
    EmptyHint->SetBounds(
        {panelX + pad, scrollTop + pad, panelW - pad * 2, titleH * 2});
    EmptyHint->SetVisible(GridSlots.empty());
  }

  if (Built)
  {
    LayoutGridInScroll();
    if (Scroll)
    {
      Scroll->LayoutContent();
    }
  }
}

void USurvivalInventoryScreen::RebuildGrid()
{
  if (!Scroll || !Catalog || !Session || !Theme)
  {
    return;
  }

  Scroll->Content().ClearChildren();
  GridSlots.clear();
  GridEntryIds.clear();

  std::vector<InventoryEntryView> all;
  const auto typeIds = Catalog->GetTypeIds(Kind);
  for (const auto &typeId : typeIds)
  {
    const auto entries = Session->GetEntries(Kind, typeId);
    all.insert(all.end(), entries.begin(), entries.end());
  }

  std::sort(all.begin(), all.end(),
            [](const InventoryEntryView &a, const InventoryEntryView &b)
            {
              if (a.label == b.label)
                return a.ref.Id < b.ref.Id;
              return a.label < b.label;
            });

  const int slotSize = Theme->HotbarSlotSize;
  for (const InventoryEntryView &view : all)
  {
    const InventoryEntryRef entry = view.ref;
    if (entry.empty || entry.Id.empty())
    {
      continue;
    }
    if (Kind == ContentKind::Block &&
        entry.kind != InventoryEntryKind::Block)
    {
      continue;
    }
    if (Kind == ContentKind::Item && entry.kind != InventoryEntryKind::Item)
    {
      continue;
    }

    auto slot = std::make_unique<UGuiSlot>(Theme);
    slot->SetBounds({0, 0, slotSize, slotSize});

    if (Icons)
    {
      const unsigned tex =
          (Kind == ContentKind::Block)
              ? Icons->GetBlockIconTexture(entry.Id)
              : Icons->GetItemIconTexture(entry.Id);
      slot->SetIconTexture(tex);
    }

    if (entry.count > 1)
    {
      slot->SetCornerHint("x" + std::to_string(entry.count));
    }
    else if (entry.count < 0)
    {
      slot->SetCornerHint("inf");
    }

    const InventoryEntryRef entryCopy = entry;
    slot->SetOnClick(
        [this, entryCopy]()
        {
          if (!Session)
          {
            return;
          }
          const size_t bar = 0;
          const size_t selectedSlot = Session->GetSelectedSlot(bar);
          (void)Session->AssignToHotbar(entryCopy, bar, selectedSlot);
        });

    UGuiSlot *ptr =
        static_cast<UGuiSlot *>(Scroll->Content().AddChild(std::move(slot)));
    GridSlots.push_back(ptr);
    GridEntryIds.push_back(entry.Id);
  }

  if (EmptyHint)
  {
    EmptyHint->SetVisible(GridSlots.empty());
  }

  Scroll->SetAfterScrollLayout([this](UGuiScrollView &)
                               { LayoutGridInScroll(); });
  Scroll->LayoutContent();
  LayoutGridInScroll();
  Built = true;
}

void USurvivalInventoryScreen::LayoutGridInScroll()
{
  if (!Scroll || !Theme)
  {
    return;
  }
  const GuiRect contentRect = Scroll->Content().GetBounds();
  const int slotSize = Theme->HotbarSlotSize;
  const int gap = Theme->HotbarSlotGap;
  const int viewportW = std::max(1, contentRect.W);
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
