#include "Gui/Screens/AnvilScreen.h"
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Game/Survival/SurvivalSystems.h"

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
#include "World/Core/World.h"

#include <memory>
#include <new>
#include <type_traits>
#include <typeinfo>

namespace cutum
{

struct UAnvilScreen::RepairAction
{
  UAnvilScreen *owner;
  void operator()() const { owner->RequestRepair(); }
};

namespace
{
std::string FormatItem(const InventoryEntryRef &entry, const ItemDefinition *definition)
{
  std::string out = entry.Id;
  out += "  wear ";
  out += std::to_string(static_cast<int>(entry.wear * 100.f));
  out += '%';
  if (entry.broken)
    out += " (broken)";
  if (definition && !definition->Repair.Materials.empty())
  {
    out += "\nCost: 1 x ";
    const auto &materials = definition->Repair.Materials;
    for (size_t i = 0; i < materials.size(); ++i)
    {
      if (i)
        out += " or ";
      out += materials[i];
    }
    out += "\nWear after: ";
    out += std::to_string(int(std::max(0.f, entry.wear - definition->Repair.Amount) * 100.f));
    out += '%';
  }
  return out;
}
} // namespace

UAnvilScreen::UAnvilScreen(UWorld *world, IUGuiIconSource *icons)
    : World(world), Icons(icons)
{
}

UAnvilScreen::~UAnvilScreen() = default;

void UAnvilScreen::Build(UGuiContext &ctx)
{
  const auto *theme = &ctx.GetTheme();
  auto panel = std::make_unique<UGuiPanel>(theme);
  panel->SetDrawBackground(true);
  panel->SetZOrder(20);
  panel->SetVisible(Visible);
  panel->ReserveChildren(5);

  auto title = std::make_unique<UGuiLabel>(theme, "Anvil");
  auto *nextTitle = title.get();
  panel->AddChild(std::move(title));

  auto slot = std::make_unique<UGuiSlot>(theme);
  auto *nextSlot = slot.get();
  if (ItemSlot)
  {
    slot->SetIconTexture(ItemSlot->GetIconTexture());
    slot->SetWearProgress(DisplayedWear);
    slot->SetBroken(DisplayedBroken);
  }
  panel->AddChild(std::move(slot));

  auto item = std::make_unique<UGuiLabel>(theme, ItemLabel ? ItemLabel->GetText() : "No item");
  auto *nextItem = item.get();
  item->SetUseSecondaryColor(true);
  panel->AddChild(std::move(item));

  auto status = std::make_unique<UGuiLabel>(theme, Status ? Status->GetText() : "");
  auto *nextStatus = status.get();
  status->SetUseSecondaryColor(true);
  panel->AddChild(std::move(status));

  auto repair = std::make_unique<UGuiButton>(theme, "Repair");
  auto *nextRepair = repair.get();
  repair->SetOnClick(RepairAction{this});
  panel->AddChild(std::move(repair));

  Theme = theme;
  Panel = panel.get();
  Title = nextTitle;
  ItemSlot = nextSlot;
  ItemLabel = nextItem;
  Status = nextStatus;
  RepairBtn = nextRepair;
  Root = std::move(panel);
  Relayout();
}

void UAnvilScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

void UAnvilScreen::SetWorld(UWorld *world)
{
  if (World == world)
    return;
  World = world;
  PendingRepair.reset();
  Display = ItemDisplay::Initial;
  DisplayedDefinitions = nullptr;
  std::string{}.swap(DisplayedId);
  if (ItemSlot)
    ItemSlot->SetIconTexture(0);
  if (Status)
    Status->Text.clear();
}

void UAnvilScreen::SetVisible(bool visible)
{
  Visible = visible;
  if (Root)
    Root->SetVisible(visible);
  if (!Visible)
  {
    PendingRepair.reset();
    if (RepairBtn)
      RepairBtn->ClearPressState();
    if (ItemSlot)
      ItemSlot->SetIconTexture(0);
    return;
  }
  Relayout();
  Update(0);
}

void UAnvilScreen::Toggle() { SetVisible(!Visible); }

void UAnvilScreen::Update(double /*dt*/) try
{
  if (Visible)
  {
    FinishRepair();
    Refresh();
  }
}
catch (const std::bad_alloc &)
{
  // A pending operation or display can be retried on the next frame.
}

void UAnvilScreen::Relayout()
{
  if (!Panel || !Theme)
  {
    return;
  }
  const int w = std::min(ViewportW - 16, Scaled(620));
  const int h = std::min(ViewportH - 16, Scaled(330));
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
    ItemLabel->SetBounds(
        {x + 24 + slotSize + 12, y + 56, w - slotSize - 60, Scaled(80)});
  }
  if (Status)
  {
    Status->SetBounds({x + 24, y + 56 + std::max(slotSize + 12, Scaled(90)),
                       w - 48, Scaled(30)});
  }
  if (RepairBtn)
  {
    RepairBtn->SetBounds({x + 40, y + h - 52, w - 80, 36});
  }
}

void UAnvilScreen::Refresh()
{
  if (!ItemSlot || !ItemLabel)
    return;
  const auto *actor = World ? World->GetControlledCreature() : nullptr;
  const auto *entry = actor ? actor->GetInventory().GetActiveEntryRef() : nullptr;
  const auto next = !World ? ItemDisplay::NoWorld
                  : !actor ? ItemDisplay::NoActor
                  : !entry || entry->kind != InventoryEntryKind::Item ? ItemDisplay::NoItem
                  : ItemDisplay::Item;
  if (next != ItemDisplay::Item)
  {
    if (next == Display)
      return;
    std::string text = next == ItemDisplay::NoWorld ? "No world"
                     : next == ItemDisplay::NoActor ? "No controlled creature"
                     : "Hold a repairable item";
    ItemLabel->Text.swap(text);
    std::string{}.swap(DisplayedId);
    DisplayedDefinitions = nullptr;
    DisplayedWear = 0.f;
    DisplayedBroken = false;
    ItemSlot->SetIconTexture(0);
    ItemSlot->SetBroken(false);
    ItemSlot->SetWearProgress(0.f);
    Display = next;
    return;
  }

  const auto *definitions = World->GetItemDefinitionStorage();
  const auto revision = UItemDefinitionStorage::ContentRevision();
  if (Display == ItemDisplay::Item && DisplayedDefinitions == definitions &&
      DisplayedRevision == revision && DisplayedId == entry->Id &&
      DisplayedWear == entry->wear && DisplayedBroken == entry->broken)
  {
    ItemSlot->SetIconTexture(Icons ? Icons->GetEntryIcon(*entry) : 0);
    return;
  }

  auto id = entry->Id;
  auto label = FormatItem(*entry, definitions ? definitions->Get(id) : nullptr);
  const auto texture = Icons ? Icons->GetEntryIcon(*entry) : 0;
  ItemLabel->Text.swap(label);
  DisplayedId.swap(id);
  DisplayedDefinitions = definitions;
  DisplayedRevision = revision;
  DisplayedWear = entry->wear;
  DisplayedBroken = entry->broken;
  ItemSlot->SetIconTexture(texture);
  ItemSlot->SetBroken(DisplayedBroken);
  ItemSlot->SetWearProgress(DisplayedWear);
  Display = next;
}

void UAnvilScreen::RequestRepair() try
{
  if (!Visible || !Status || PendingRepair)
    return;
  if (!World)
  {
    Status->SetText("No world");
    return;
  }
  const auto *actor = World->GetControlledCreature();
  if (!actor || !World->GetItemDefinitionStorage())
  {
    Status->SetText("Cannot repair");
    return;
  }
  const auto &inventory = actor->GetInventory();
  const auto *entry = inventory.GetActiveEntryRef();
  PendingRepair = RepairRequest{actor->GetId(), entry ? entry->instanceId : 0,
                                inventory.GetActiveBarIndex(), inventory.GetActiveSlotIndex()};
  FinishRepair();
  Refresh();
}
catch (const std::bad_alloc &)
{
  // Keep the accepted target pending if preparation was incomplete.
}

void UAnvilScreen::FinishRepair()
{
  if (!PendingRepair)
    return;
  const auto request = *PendingRepair;
  auto *actor = World ? World->GetControlledCreature() : nullptr;
  auto *items = World ? World->GetItemDefinitionStorage() : nullptr;
  std::string result;
  if (!actor || !items || actor->GetId() != request.Actor)
    result = "Cannot repair";
  else
  {
    auto &inventory = actor->GetInventory();
    const auto *entry = inventory.GetActiveEntryRef();
    if ((entry ? entry->instanceId : 0) != request.Instance ||
        inventory.GetActiveBarIndex() != request.Row ||
        inventory.GetActiveSlotIndex() != request.Slot)
      result = "repair_source_changed";
    else
    {
      const auto &stations = USurvivalSystems::Instance();
      const auto position = actor->GetBodyOrigin();
      if (!stations.NearStation(*World, position, "anvil") &&
          !stations.NearStation(*World, position, "anvil_slightly_damaged") &&
          !stations.NearStation(*World, position, "anvil_very_damaged"))
        result = "Requires nearby anvil";
      else
      {
        result = "Repaired";
        std::string reason;
        if (!inventory.RepairActive(*items, reason))
          result.swap(reason);
      }
    }
  }
  PendingRepair.reset();
  Status->Text.swap(result);
}

AnvilScreenStorage UAnvilScreen::InspectOwnedStorage(
    const UGuiContext &context, const UWorld *world, const IUGuiIconSource *icons) const noexcept
{
  AnvilScreenStorage out;
  out.pending = PendingRepair.has_value();
  const auto *theme = &context.GetTheme();
  if (typeid(*this) != typeid(UAnvilScreen) || Context != &context || World != world ||
      Icons != icons || Theme != theme || !Root || Root.get() != Panel ||
      typeid(*Root) != typeid(UGuiPanel) || Panel->Theme != theme ||
      Panel->IsVisible() != Visible)
    return out;

  const auto &children = Panel->GetChildren();
  const auto matches = [theme](const auto &child, const auto *alias)
  {
    using Widget = std::remove_cv_t<std::remove_pointer_t<decltype(alias)>>;
    return child && child.get() == alias && typeid(*child) == typeid(Widget) &&
           child->GetChildren().empty() && alias->Theme == theme;
  };
  if (children.size() != 5 || !matches(children[0], Title) || !matches(children[1], ItemSlot) ||
      !matches(children[2], ItemLabel) || !matches(children[3], Status) ||
      !matches(children[4], RepairBtn) || ItemSlot->OnClick || ItemSlot->OnActivate ||
      ItemSlot->OnBeginDrag)
    return out;

  const bool profile = StlSourceStorage::SupportedProfile();
  const auto action = InspectKnownOwnerCallback<RepairAction>(RepairBtn->OnClick, this, profile);
  StlSourceStorage backing, text, total;
  backing.Buffer(children.capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  for (const auto &child : children)
    backing.Buffer(child->GetChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  for (const auto *label : {Title, ItemLabel, Status})
    text.Text(label->GetText());
  text.Text(RepairBtn->GetLabel());
  text.Text(ItemSlot->Label);
  text.Text(ItemSlot->CornerHint);
  text.Text(ItemSlot->CountHint);
  text.Text(DisplayedId);
  out.screenBytes = sizeof(*this);
  out.widgets = 1 + children.size();
  out.widgetBytes = sizeof(UGuiPanel) + sizeof(UGuiSlot) + 3 * sizeof(UGuiLabel) + sizeof(UGuiButton);
  out.childrenBytes = backing.bytes.requested;
  out.textBytes = text.bytes.requested;
  out.callbackBytes = action.requests;
  for (const auto bytes : {out.screenBytes, out.widgetBytes, out.childrenBytes, out.textBytes, out.callbackBytes})
    total.Add(bytes);
  out.requested = total.bytes.requested;
  out.supported = action.present && action.supported && backing.bytes.supported &&
                  text.bytes.supported && total.bytes.supported;
  return out;
}

} // namespace cutum
