#include "Gui/Screens/SurvivalInventoryScreen.h"

#include "Game/GameSession.h"
#include "Gui/Interfaces/IUContentCatalog.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Core/GuiContext.h"
#include "Game/Inventory/SlotInteraction.h"

#include <algorithm>
#include <iostream>
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

  auto rootShell = std::make_unique<UGuiPanel>(Theme);
  rootShell->SetDrawBackground(false);
  rootShell->SetClipChildren(false);
  RootShell = rootShell.get();

  auto panel = std::make_unique<UGuiPanel>(Theme);
  panel->SetDrawBackground(true);
  panel->SetZOrder(10);
  Panel = panel.get();

  auto title = std::make_unique<UGuiLabel>(Theme, "Backpack (items)");
  title->SetUseSecondaryColor(true);
  Panel->AddChild(std::move(title));

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
  LayoutGridInScroll();
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
  }
}

void USurvivalInventoryScreen::Toggle() { SetVisible(!Visible); }

void USurvivalInventoryScreen::Update(double dt)
{
  (void)dt;
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

  const auto typeIds = Catalog->GetTypeIds(ContentKind::Item);
  std::vector<InventoryEntryView> all;
  for (const auto &typeId : typeIds)
  {
    const auto entries = Session->GetEntries(ContentKind::Item, typeId);
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
  for (size_t i = 0; i < all.size(); ++i)
  {
    const InventoryEntryRef entry = all[i].ref;
    if (entry.empty || entry.Id.empty() || entry.kind != InventoryEntryKind::Item)
    {
      continue;
    }

    auto slot = std::make_unique<UGuiSlot>(Theme);
    slot->SetBounds({0, 0, slotSize, slotSize});

    if (Icons)
    {
      const unsigned tex = Icons->GetItemIconTexture(entry.Id);
      slot->SetIconTexture(tex);
    }

    if (entry.count > 1)
    {
      slot->SetCornerHint("x" + std::to_string(entry.count));
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
      std::max(viewportH, rows * (slotSize + gap) - (rows > 0 ? gap : 0) +
                               8);
  Scroll->Content().SetBounds(
      {contentRect.X, contentTop, viewportW, contentHeight});
}

} // namespace cutum

