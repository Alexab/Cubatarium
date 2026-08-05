#include "Gui/Screens/AnvilScreen.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
#include "World/Core/World.h"

#include <memory>
#include <sstream>

namespace cutum
{

UAnvilScreen::UAnvilScreen(UWorld *world, IUGuiIconSource *icons)
    : World(world), Icons(icons)
{
}

UAnvilScreen::~UAnvilScreen() = default;

void UAnvilScreen::Build(UGuiContext &ctx)
{
  Theme = &ctx.GetTheme();

  auto panel = std::make_unique<UGuiPanel>(Theme);
  panel->SetDrawBackground(true);
  panel->SetZOrder(20);
  Panel = panel.get();

  auto title = std::make_unique<UGuiLabel>(Theme, "Anvil");
  Title = title.get();
  Panel->AddChild(std::move(title));

  auto slot = std::make_unique<UGuiSlot>(Theme);
  ItemSlot = slot.get();
  Panel->AddChild(std::move(slot));

  auto item = std::make_unique<UGuiLabel>(Theme, "No item");
  item->SetUseSecondaryColor(true);
  ItemLabel = item.get();
  Panel->AddChild(std::move(item));

  auto status = std::make_unique<UGuiLabel>(Theme, "");
  status->SetUseSecondaryColor(true);
  Status = status.get();
  Panel->AddChild(std::move(status));

  auto repair = std::make_unique<UGuiButton>(Theme, "Repair");
  RepairBtn = repair.get();
  RepairBtn->SetOnClick([this]() { TryRepair(); });
  Panel->AddChild(std::move(repair));

  Root = std::move(panel);
  SetVisible(false);
}

void UAnvilScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

void UAnvilScreen::SetVisible(bool visible)
{
  Visible = visible;
  if (Root)
  {
    Root->SetVisible(visible);
  }
  if (visible)
  {
    Refresh();
    Relayout();
  }
}

void UAnvilScreen::Toggle() { SetVisible(!Visible); }

void UAnvilScreen::Update(double /*dt*/)
{
  if (Visible)
  {
    Refresh();
  }
  if (Status && Status->GetText() != StatusText)
  {
    Status->SetText(StatusText);
  }
}

void UAnvilScreen::Relayout()
{
  if (!Panel || !Theme)
  {
    return;
  }
  const int w = 360;
  const int h = 240;
  const int x = (ViewportW - w) / 2;
  const int y = (ViewportH - h) / 2;
  Panel->SetBounds({x, y, w, h});
  if (Title)
  {
    Title->SetBounds({x + 16, y + 12, w - 32, 28});
  }
  const int slotSize = Theme->HotbarSlotSize;
  if (ItemSlot)
  {
    ItemSlot->SetBounds({x + 24, y + 56, slotSize, slotSize});
  }
  if (ItemLabel)
  {
    ItemLabel->SetBounds({x + 24 + slotSize + 12, y + 64, w - slotSize - 60, 28});
  }
  if (Status)
  {
    Status->SetBounds({x + 24, y + 56 + slotSize + 12, w - 48, 22});
  }
  if (RepairBtn)
  {
    RepairBtn->SetBounds({x + 40, y + h - 52, w - 80, 36});
  }
}

void UAnvilScreen::Refresh()
{
  if (!ItemSlot || !ItemLabel)
  {
    return;
  }
  if (!World)
  {
    ItemLabel->SetText("No world");
    ItemSlot->SetIconTexture(0);
    ItemSlot->SetBroken(false);
    ItemSlot->SetWearProgress(0.f);
    return;
  }
  UCreature *creature = World->GetControlledCreature();
  if (!creature)
  {
    ItemLabel->SetText("No controlled creature");
    return;
  }
  const InventoryEntryRef *active =
      creature->GetInventory().GetActiveEntryRef();
  if (!active || active->empty || active->kind != InventoryEntryKind::Item)
  {
    ItemLabel->SetText("Hold a repairable item");
    ItemSlot->SetIconTexture(0);
    ItemSlot->SetBroken(false);
    ItemSlot->SetWearProgress(0.f);
    return;
  }
  if (Icons)
  {
    ItemSlot->SetIconTexture(Icons->GetItemIconTexture(active->Id));
  }
  ItemSlot->SetBroken(active->broken);
  ItemSlot->SetWearProgress(active->wear);
  std::ostringstream oss;
  oss << active->Id << "  wear " << static_cast<int>(active->wear * 100.f)
      << "%";
  if (active->broken)
  {
    oss << " (broken)";
  }
  ItemLabel->SetText(oss.str());
}

void UAnvilScreen::TryRepair()
{
  if (!World)
  {
    StatusText = "No world";
    return;
  }
  UCreature *creature = World->GetControlledCreature();
  UItemDefinitionStorage *items = World->GetItemDefinitionStorage();
  if (!creature || !items)
  {
    StatusText = "Cannot repair";
    return;
  }
  auto &bars = creature->GetInventory().GetHotbarsMutable();
  const size_t bar = creature->GetInventory().GetActiveBarIndex();
  const size_t slot = creature->GetInventory().GetActiveSlotIndex();
  if (bar >= bars.size() || slot >= bars[bar].slots.size() ||
      bars[bar].slots[slot].empty)
  {
    StatusText = "Empty active slot";
    return;
  }
  InventoryEntryRef &entry = bars[bar].slots[slot].entry;
  if (entry.kind != InventoryEntryKind::Item)
  {
    StatusText = "Active slot is not an item";
    return;
  }
  const ItemDefinition *def = items->Get(entry.Id);
  if (!def)
  {
    StatusText = "Unknown item";
    return;
  }
  std::string material;
  if (!def->Repair.Materials.empty())
  {
    material = def->Repair.Materials.front();
    auto &storage = creature->GetInventory().GetStorageMutable();
    const auto it = storage.find(material);
    if (it == storage.end() || (it->second >= 0 && it->second < 1))
    {
      StatusText = "Need material: " + material;
      return;
    }
    if (it->second > 0)
    {
      it->second -= 1;
      if (it->second <= 0)
      {
        storage.erase(it);
      }
    }
  }
  if (!TryRepairItem(entry, *def, material))
  {
    StatusText = "Repair failed";
    return;
  }
  StatusText = "Repaired " + entry.Id;
  Refresh();
}

} // namespace cutum
