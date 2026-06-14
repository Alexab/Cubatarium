#include "CreativePaletteScreen.h"
#include "Game/GameSession.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include "Gui/Interfaces/IGuiIconSource.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiTabBar.h"
#include "Game/Inventory/SlotInteraction.h"

#include <algorithm>

namespace cutum
{

UCreativePaletteScreen::UCreativePaletteScreen(IContentCatalog *catalog,
                                               UGameSession *session,
                                               IGuiIconSource *icons)
    : catalog_(catalog), session_(session), icons_(icons)
{
}

bool UCreativePaletteScreen::PickSlot(int x, int y, SlotAddress &out) const
{
  if (!visible_)
  {
    return false;
  }
  for (size_t i = 0; i < gridSlots_.size(); ++i)
  {
    const UGuiSlot *slot = gridSlots_[i];
    if (!slot || !slot->IsVisible() || !slot->GetBounds().Contains(x, y))
    {
      continue;
    }
    if (i >= gridEntryIds_.size())
    {
      return false;
    }
    out.surface = SlotSurface::PaletteGrid;
    out.paletteKind = kind_;
    out.entryId = gridEntryIds_[i];
    out.bar = 0;
    out.slot = 0;
    return true;
  }
  return false;
}

void UCreativePaletteScreen::SetVisible(bool visible)
{
  visible_ = visible;
  if (root_)
  {
    root_->SetVisible(visible);
  }
}

void UCreativePaletteScreen::Toggle() { SetVisible(!visible_); }

void UCreativePaletteScreen::Build(UGuiContext &ctx)
{
  theme_ = &ctx.GetTheme();
  auto panel = std::make_unique<UGuiPanel>(theme_);
  panel->SetVisible(false);
  panel_ = panel.get();

  auto mainTabs = std::make_unique<UGuiTabBar>(theme_);
  mainTabs->SetTabs({"Blocks", "Objects", "Creatures", "Skins"});
  mainTabs_ = mainTabs.get();
  mainTabs->SetOnTabChanged(
      [this](int tab)
      {
        switch (tab)
        {
        case 0:
          kind_ = ContentKind::Block;
          break;
        case 1:
          kind_ = ContentKind::UObject;
          break;
        case 2:
          kind_ = ContentKind::UCreature;
          break;
        default:
          kind_ = ContentKind::Skin;
          break;
        }
        if (catalog_)
        {
          const auto types = catalog_->GetTypeIds(kind_);
          activeTypeId_ = types.empty() ? "misc" : types.front();
        }
        selectedEntryId_.clear();
        built_ = false;
      });

  auto subTabs = std::make_unique<UGuiTabBar>(theme_);
  subTabs_ = subTabs.get();
  subTabs->SetOnTabChanged(
      [this](int tab)
      {
        if (catalog_)
        {
          const auto types = catalog_->GetTypeIds(kind_);
          if (tab >= 0 && tab < static_cast<int>(types.size()))
          {
            activeTypeId_ = types[static_cast<size_t>(tab)];
          }
        }
        selectedEntryId_.clear();
        built_ = false;
      });

  auto scroll = std::make_unique<UGuiScrollView>(theme_);
  scroll_ = scroll.get();

  panel->AddChild(std::move(mainTabs));
  panel->AddChild(std::move(subTabs));
  panel->AddChild(std::move(scroll));
  root_ = std::move(panel);
  kind_ = ContentKind::Block;
  activeTypeId_ = "misc";
  built_ = false;
}

void UCreativePaletteScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  RelayoutPanel();
}

void UCreativePaletteScreen::RelayoutPanel()
{
  if (!panel_)
  {
    return;
  }
  const int panelW = viewportW_ * 60 / 100;
  const int panelH = viewportH_ * 70 / 100;
  const int panelX = (viewportW_ - panelW) / 2;
  const int panelY = (viewportH_ - panelH) / 2;
  panel_->SetBounds({panelX, panelY, panelW, panelH});

  if (mainTabs_)
  {
    mainTabs_->SetBounds({panelX + 8, panelY + 8, panelW - 16, 28});
  }
  if (subTabs_)
  {
    subTabs_->SetBounds({panelX + 8, panelY + 40, panelW - 16, 28});
  }
  if (scroll_)
  {
    const int scrollH = std::max(0, panelH - 84);
    scroll_->SetBounds({panelX + 8, panelY + 76, panelW - 16, scrollH});
    if (built_)
    {
      scroll_->LayoutContent();
      LayoutGridInScroll();
    }
  }
}

void UCreativePaletteScreen::Update(double /*dt*/)
{
  if (!visible_ || !panel_ || !catalog_ || !theme_)
  {
    return;
  }
  RelayoutPanel();

  if (subTabs_)
  {
    const auto types = catalog_->GetTypeIds(kind_);
    std::vector<std::string> labels;
    for (const auto &id : types)
    {
      labels.push_back(catalog_->GetTypeDisplayName(id));
    }
    if (labels.empty())
    {
      labels.push_back("Misc");
    }
    subTabs_->SetTabs(labels);
    if (!types.empty())
    {
      auto it = std::find(types.begin(), types.end(), activeTypeId_);
      if (it == types.end())
      {
        activeTypeId_ = types.front();
        subTabs_->SetActiveTab(0);
      }
      else
      {
        subTabs_->SetActiveTab(
            static_cast<int>(std::distance(types.begin(), it)));
      }
    }
    else
    {
      activeTypeId_.clear();
      subTabs_->SetActiveTab(0);
    }
  }

  if (!built_)
  {
    RebuildGrid();
  }
}

void UCreativePaletteScreen::RebuildGrid()
{
  if (!scroll_ || !catalog_ || !session_ || !theme_)
  {
    return;
  }
  scroll_->Content().ClearChildren();
  gridSlots_.clear();
  gridEntryIds_.clear();

  const auto entries = catalog_->GetEntries(
      kind_, activeTypeId_.empty() ? "misc" : activeTypeId_);
  const int slotSize = theme_->hotbarSlotSize;
  for (size_t i = 0; i < entries.size(); ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(theme_, slotSize);
    slot->SetBounds({0, 0, slotSize, slotSize});
    const std::string entryId = entries[i].id;
    slot->SetSelected(entryId == selectedEntryId_);
    if (icons_)
    {
      GLuint tex = 0;
      if (kind_ == ContentKind::Block)
      {
        tex = icons_->GetBlockIconTexture(entryId);
      }
      else if (kind_ == ContentKind::UObject)
      {
        tex = icons_->GetPrefabIconTexture(entryId);
      }
      else if (kind_ == ContentKind::UCreature)
      {
        tex = icons_->GetCreatureIconTexture(entryId);
      }
      else if (kind_ == ContentKind::Skin)
      {
        tex = icons_->GetSkinIconTexture(entryId);
      }
      slot->SetIconTexture(tex);
    }
    InventoryEntryRef entry;
    entry.empty = false;
    entry.id = entryId;
    switch (kind_)
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
    address.paletteKind = kind_;
    address.entryId = entryId;

    slot->SetOnClick(
        [this, entry]()
        {
          if (!session_)
          {
            return;
          }
          selectedEntryId_ = entry.id;
          session_->BeginPendingAssignment(entry);
          built_ = false;
        });
    slot->SetOnBeginDrag(
        [this, address, entry]()
        {
          if (session_)
          {
            session_->BeginDragFromSlot(address, entry);
          }
        });
    UGuiSlot *ptr =
        static_cast<UGuiSlot *>(scroll_->Content().AddChild(std::move(slot)));
    gridSlots_.push_back(ptr);
    gridEntryIds_.push_back(entryId);
  }

  scroll_->SetAfterScrollLayout([this](UGuiScrollView &)
                                { LayoutGridInScroll(); });
  scroll_->LayoutContent();
  LayoutGridInScroll();
  built_ = true;
}

void UCreativePaletteScreen::LayoutGridInScroll()
{
  if (!scroll_ || !theme_)
  {
    return;
  }
  const GuiRect contentRect = scroll_->Content().GetBounds();
  const int slotSize = theme_->hotbarSlotSize;
  const int gap = theme_->hotbarSlotGap;
  const int viewportW = contentRect.w;
  const int viewportH = std::max(1, scroll_->GetBounds().h);
  const int denom = std::max(1, slotSize + gap);
  const int cols = std::max(1, (viewportW + gap) / denom);
  const int contentTop = contentRect.y;

  int x = contentRect.x;
  int y = contentTop;
  for (size_t i = 0; i < gridSlots_.size(); ++i)
  {
    UGuiSlot *slot = gridSlots_[i];
    if (!slot)
    {
      continue;
    }
    slot->SetBounds({x, y, slotSize, slotSize});
    if ((i + 1) % static_cast<size_t>(cols) == 0)
    {
      x = contentRect.x;
      y += slotSize + gap;
    }
    else
    {
      x += slotSize + gap;
    }
  }

  const int rows =
      gridSlots_.empty()
          ? 0
          : static_cast<int>((gridSlots_.size() + cols - 1) / cols);
  const int contentHeight =
      std::max(viewportH, rows * (slotSize + gap) - (rows > 0 ? gap : 0) + 8);
  scroll_->Content().SetBounds(
      {contentRect.x, contentTop, viewportW, contentHeight});
}

} // namespace cutum
