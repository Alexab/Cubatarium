#include "Game/GameSession.h"
#include "App/Application.h"
#include "Audio/AudioApi.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Commands/WorldCommands.h"
#include "Content/ContentType.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Influence/StatusEffectSystem.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Game/Interfaces/IUGameContent.h"
#include "Game/Inventory/InventoryOperations.h"
#include "Game/ModePolicy.h"
#include "Gui/Screens/AnvilScreen.h"
#include "Gui/Screens/CharacterSheetScreen.h"
#include "Gui/Screens/CraftingScreen.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Screens/SurvivalInventoryScreen.h"
#include "Gui/Screens/WorldGenPaletteScreen.h"
#include "Items/ItemDefinitionStorage.h"
#include "Render/Camera/Camera.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/Raycast/BlockRaycast.h"
#include "WorldGen/Core/WorldGenContentReload.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>

namespace cutum
{

namespace
{

/// Inventory drag/drop clicks — UI-bus sounds, no spatialization.
void PostInventoryClickAudio(AudioEventKind kind)
{
  UAudioEvent audio;
  audio.Kind = kind;
  UAudioApi::Post(audio);
}

UCreatureInventory *GetControlledInventory(UWorld *world)
{
  if (!world)
  {
    return nullptr;
  }
  if (UCreature *creature = world->GetControlledCreature())
  {
    auto &inv = creature->GetInventory();
    inv.SetItemDefinitions(world->GetItemDefinitionStorage());
    inv.SetCreative(world->GetGameMode() == WorldGameMode::Creative);
    return &inv;
  }
  return nullptr;
}

const UCreatureInventory *GetControlledInventory(const UWorld *world)
{
  if (!world)
  {
    return nullptr;
  }
  if (const UCreature *creature = world->GetControlledCreature())
  {
    return &creature->GetInventory();
  }
  return nullptr;
}

} // namespace

UGameSession::UGameSession(UApplication *application,
                           std::shared_ptr<UWorld> world)
    : Application(application), World(std::move(world))
{
}

void UGameSession::InitializeCatalog(const std::string &typesJsonPath,
                                     const IUGameContent &content)
{
  ContentCatalog.LoadTypes(typesJsonPath);
  ReindexBlockCatalog(content);
}

void UGameSession::ReindexBlockCatalog(const IUGameContent &content)
{
  ContentCatalog.IndexBlocks(content.Blocks());
  ContentCatalog.IndexObjects(content.Objects());
  ContentCatalog.IndexItems(content.Items());
  if (World)
  {
    ContentCatalog.IndexCreatures(content.Creatures());
    if (const auto &skinDefs = World->GetSkinDefinitionStorage())
    {
      ContentCatalog.IndexSkins(*skinDefs);
    }
  }
}

void UGameSession::RegisterCommands()
{
  RegisterWorldCommands(*this, UCommandRegistry);
}

void UGameSession::LoadLastWorld()
{
  if (Application)
  {
    Application->ScheduleEnterGame();
  }
}

void UGameSession::ResumeGame()
{
  if (Application)
  {
    Application->RequestEnterGame();
  }
}

void UGameSession::OpenLoadWorld()
{
  if (Application)
  {
    Application->ShowLoadWorld();
  }
}

void UGameSession::OpenNewWorld()
{
  if (Application)
  {
    Application->ShowNewWorld();
  }
}

void UGameSession::QuitApplication()
{
  if (Application)
  {
    Application->ScheduleQuit();
  }
}

void UGameSession::OpenSettings()
{
  if (Application)
  {
    Application->ShowSettings();
  }
}

void UGameSession::OpenWorldSettings()
{
  if (Application)
  {
    Application->ShowWorldSettings();
  }
}

bool UGameSession::HasPausedSession() const
{
  return Application && Application->HasWorldSession();
}

int UGameSession::GetHotbarCountSetting() const
{
  return Application ? Application->GetHotbarCountSetting() : 1;
}

void UGameSession::SetHotbarCountSetting(int count)
{
  if (Application)
  {
    Application->SetHotbarCountSetting(count);
  }
}

size_t UGameSession::GetBarCount() const
{
  UCreatureInventory *inv = GetControlledInventory(World.get());
  if (!inv)
  {
    return 0;
  }
  inv->EnsureHotbarCount(static_cast<size_t>(GetHotbarCountSetting()));
  return std::min(inv->GetHotbarCount(),
                  static_cast<size_t>(GetHotbarCountSetting()));
}

std::array<HotbarSlotView, 10> UGameSession::GetBarSlots(size_t barIndex) const
{
  std::array<HotbarSlotView, 10> slots{};
  UCreatureInventory *inv = GetControlledInventory(World.get());
  if (!inv || barIndex >= inv->GetHotbarCount())
  {
    return slots;
  }
  const HotbarBar &bar = inv->GetHotbar(barIndex);
  const size_t activeBar = inv->GetActiveBarIndex();
  const size_t activeIndex = inv->GetActiveSlotIndex(barIndex);
  for (size_t i = 0; i < slots.size(); ++i)
  {
    const HotbarSlot &slot = bar.slots[i];
    if (!slot.empty && !slot.entry.Id.empty())
    {
      slots[i].Id = slot.entry.Id;
      slots[i].label = HumanizeBlockName(slot.entry.Id);
      slots[i].entryKind = slot.entry.kind;
      slots[i].isBlock = (slot.entry.kind == InventoryEntryKind::Block);
      slots[i].wear = slot.entry.wear;
      slots[i].broken = slot.entry.broken;
      slots[i].CountLabel = slot.entry.IsCatalogReference()
                                ? ""
                                : std::to_string(slot.entry.count);
    }
    slots[i].selected = (barIndex == activeBar) && (i == activeIndex);
    if (barIndex == 0)
    {
      slots[i].hotkey = static_cast<int>(i);
    }
  }
  return slots;
}

size_t UGameSession::GetSelectedSlot(size_t barIndex) const
{
  const UCreatureInventory *inv = GetControlledInventory(World.get());
  if (!inv)
  {
    return 0;
  }
  const size_t slot = inv->GetActiveSlotIndex(barIndex);
  return slot < 10 ? slot : 0;
}

void UGameSession::SelectSlot(size_t barIndex, size_t slotIndex)
{
  if (UCreatureInventory *inv = GetControlledInventory(World.get()))
  {
    if (World && (inv->GetActiveBarIndex() != barIndex ||
                  inv->GetActiveSlotIndex() != slotIndex))
      World->CancelBreakSession();
    inv->SetActiveSlot(barIndex, slotIndex);
  }
}

bool UGameSession::AssignSlot(size_t barIndex, size_t slotIndex,
                              const InventoryEntryRef &entry)
{
  UCreatureInventory *inv = GetControlledInventory(World.get());
  if (!inv)
  {
    return false;
  }
  inv->EnsureHotbarCount(static_cast<size_t>(GetHotbarCountSetting()));
  const bool ok = GetInventoryMode() == InventoryMode::Creative &&
                          entry.IsCatalogReference()
                      ? inv->AssignToHotbar(barIndex, slotIndex, entry)
                      : inv->AssignOwned(barIndex, slotIndex, entry);
  if (!ok && Application)
    Application->ShowInteractionMessage(
        "Cannot assign: item unavailable or inventory full");
  return ok;
}

void UGameSession::BeginPendingAssignment(const InventoryEntryRef &entry)
{
  PendingAssignment = entry;
}

bool UGameSession::HasPendingAssignment() const
{
  return PendingAssignment.has_value() && !PendingAssignment->empty;
}

bool UGameSession::ApplyPendingAssignment(size_t barIndex, size_t slotIndex)
{
  if (!PendingAssignment.has_value())
  {
    return false;
  }
  if (!CanAssignToHotbar(*PendingAssignment, barIndex, slotIndex))
  {
    return false;
  }
  const bool ok = AssignSlot(barIndex, slotIndex, *PendingAssignment);
  if (ok)
  {
    SelectSlot(barIndex, slotIndex);
    PendingAssignment.reset();
  }
  return ok;
}

void UGameSession::ClearPendingAssignment() { PendingAssignment.reset(); }

namespace
{

bool SameSlotAddress(const SlotAddress &a, const SlotAddress &b)
{
  if (a.surface != b.surface)
  {
    return false;
  }
  if (a.surface == SlotSurface::Hotbar)
  {
    return a.bar == b.bar && a.slot == b.slot;
  }
  if (a.surface == SlotSurface::PaletteGrid)
  {
    return a.paletteKind == b.paletteKind && a.entryId == b.entryId;
  }
  if (a.surface == SlotSurface::CharacterArmor)
  {
    return a.slot == b.slot;
  }
  if (a.surface == SlotSurface::CharacterOffhand)
  {
    return true;
  }
  return true;
}

} // namespace

bool UGameSession::OnPrimaryHotbarKey(int slotIndex)
{
  if (slotIndex < 0 || slotIndex >= 10)
  {
    return false;
  }
  if (HasPendingAssignment())
  {
    return ApplyPendingAssignment(0, static_cast<size_t>(slotIndex));
  }
  auto *inv = GetControlledInventory(World.get());
  SelectSlot(inv ? inv->GetActiveBarIndex() : 0,
             static_cast<size_t>(slotIndex));
  return true;
}

InventoryEntryRef UGameSession::GetHotbarEntryRef(size_t barIndex,
                                                  size_t slotIndex) const
{
  InventoryEntryRef entry;
  const UCreatureInventory *inv = GetControlledInventory(World.get());
  if (!inv || barIndex >= inv->GetHotbarCount() || slotIndex >= 10)
  {
    return entry;
  }
  const HotbarSlot &slot = inv->GetHotbar(barIndex).slots[slotIndex];
  if (slot.empty || slot.entry.empty)
  {
    return entry;
  }
  return slot.entry;
}

InventoryEntryRef UGameSession::GetArmorEntryRef(size_t armorSlot) const
{
  InventoryEntryRef entry;
  const UCreatureInventory *inv = GetControlledInventory(World.get());
  if (!inv || armorSlot >= UCreatureInventory::kArmorSlotCount)
  {
    return entry;
  }
  return inv->GetEquippedArmor(armorSlot);
}

InventoryEntryRef UGameSession::GetOffhandEntryRef() const
{
  InventoryEntryRef entry;
  const UCreatureInventory *inv = GetControlledInventory(World.get());
  if (!inv)
  {
    return entry;
  }
  return inv->GetEquippedOffhand();
}

void UGameSession::BeginDragFromSlot(const SlotAddress &source,
                                     const InventoryEntryRef &entry)
{
  if (entry.empty)
  {
    return;
  }
  // A new drag supersedes click-to-assign pending state.
  PendingAssignment.reset();
  Drag.Active = true;
  Drag.entry = entry;
  Drag.source = source;
  if (auto *inv = GetControlledInventory(World.get()))
  {
    Drag.sourceRevision = inv->GetRevision();
    Drag.sourceCreative = inv->IsCreative();
  }
  if (World && World->GetControlledCreature())
    Drag.sourceOwner = World->GetControlledCreature()->GetId();
  PostInventoryClickAudio(AudioEventKind::Pickup);
}

bool UGameSession::IsDragging() const { return Drag.Active; }

void UGameSession::CancelDrag() { Drag = DragState{}; }

bool UGameSession::DropOnSlot(const SlotAddress &target, int units)
{
  if (!Drag.Active || target.surface == SlotSurface::None)
  {
    CancelDrag();
    return false;
  }
  const DragState drag = Drag;
  CancelDrag();
  auto *inventory = GetControlledInventory(World.get());
  const auto *items = World ? World->GetItemDefinitionStorage() : nullptr;
  if (!inventory || !items)
    return false;
  if (drag.source.surface == SlotSurface::PaletteGrid &&
      !inventory->IsCreative())
    return false;
  if (drag.source.surface != SlotSurface::PaletteGrid &&
      (!UInventoryOperations::Matches(*inventory, drag.sourceRevision,
                                      drag.sourceCreative) ||
       !World->GetControlledCreature() ||
       World->GetControlledCreature()->GetId() != drag.sourceOwner))
    return false;
  if (SameSlotAddress(target, drag.source))
    return true;
  auto trial = *inventory;
  auto destination = target;
  const auto entry = drag.entry;
  if (destination.surface == SlotSurface::CharacterDoll)
  {
    const auto *def =
        entry.kind == InventoryEntryKind::Item ? items->Get(entry.Id) : nullptr;
    if (def && !def->Armor.Slots.empty())
    {
      const char *names[] = {"head", "chest", "legs", "feet"};
      for (size_t i = 0; i < 4; ++i)
        if (def->Armor.Slots.front() == names[i])
        {
          destination.surface = SlotSurface::CharacterArmor;
          destination.slot = i;
          break;
        }
    }
    if (destination.surface == SlotSurface::CharacterDoll)
    {
      destination.surface = SlotSurface::Hotbar;
      destination.bar = trial.GetActiveBarIndex();
      destination.slot = trial.GetActiveSlotIndex();
    }
  }
  const auto releaseSource = [&]()
  {
    if (drag.source.surface == SlotSurface::CharacterArmor)
    {
      trial.UnequipArmor(drag.source.slot, *items);
      return trial.GetEquippedArmor(drag.source.slot).empty;
    }
    if (drag.source.surface == SlotSurface::CharacterOffhand)
    {
      trial.UnequipOffhand(*items);
      return trial.GetEquippedOffhand().empty;
    }
    return true;
  };
  bool ok = false;
  if (destination.surface == SlotSurface::Hotbar)
  {
    if (drag.source.surface == SlotSurface::Hotbar)
    {
      const size_t from = drag.source.bar * 10 + drag.source.slot;
      const auto actual = trial.GetSlot(from);
      if (actual.instanceId != entry.instanceId || actual.empty)
        return false;
      int amount = units > 0 ? std::min(units, entry.count) : entry.count;
      auto result = trial.Move(from, destination.bar * 10 + destination.slot,
                               actual.count == amount ? -1 : amount);
      ok = result.transferred > 0;
      if (ok && amount < entry.count && !trial.GetSlot(from).empty)
      {
        Drag = drag;
        Drag.entry = trial.GetSlot(from);
        Drag.entry.count = entry.count - result.transferred;
      }
    }
    else if (releaseSource())
      ok = drag.source.surface == SlotSurface::PaletteGrid && trial.IsCreative()
               ? trial.AssignToHotbar(destination.bar, destination.slot, entry)
               : trial.AssignOwned(destination.bar, destination.slot, entry);
    if (ok)
      trial.SetActiveSlot(destination.bar, destination.slot);
  }
  else if (destination.surface == SlotSurface::CharacterArmor)
  {
    if (releaseSource())
      ok = trial.EquipArmor(destination.slot, entry, *items);
  }
  else if (destination.surface == SlotSurface::CharacterOffhand)
  {
    if (releaseSource())
      ok = trial.EquipOffhand(entry, *items);
  }
  else if (destination.surface == SlotSurface::BackpackReturn)
  {
    if (drag.source.surface == SlotSurface::Hotbar)
    {
      for (size_t i = 10; i < UCreatureInventory::kSlotCount; ++i)
        if (trial.GetSlot(i).empty)
        {
          ok = static_cast<bool>(
              trial.Move(drag.source.bar * 10 + drag.source.slot, i));
          break;
        }
    }
    else
      ok = releaseSource();
  }
  if (ok)
  {
    *inventory = std::move(trial);
    PostInventoryClickAudio(AudioEventKind::UiClick);
  }
  if (!ok && Application)
    Application->ShowInteractionMessage(
        "Cannot move or equip: check compatibility and free space");
  return ok;
}

void UGameSession::InventoryClick(size_t slot, bool half, bool quick,
                                  bool collect)
{
  auto *inv = GetControlledInventory(World.get());
  if (!inv || slot >= 40)
    return;
  SlotAddress address;
  address.surface = SlotSurface::Hotbar;
  address.bar = slot / 10;
  address.slot = slot % 10;
  if (collect && IsDragging() && SameSlotAddress(Drag.source, address))
  {
    CancelDrag();
    inv->CollectMatching(slot);
    return;
  }
  if (IsDragging())
  {
    DropOnSlot(address, half ? 1 : -1);
    return;
  }
  const auto entry = inv->GetSlot(slot);
  if (entry.empty)
    return;
  if (collect)
  {
    inv->CollectMatching(slot);
    return;
  }
  if (quick)
  {
    const auto *items = World->GetItemDefinitionStorage();
    const auto *def =
        entry.kind == InventoryEntryKind::Item ? items->Get(entry.Id) : nullptr;
    if (def && !def->Armor.Slots.empty())
    {
      const char *names[] = {"head", "chest", "legs", "feet"};
      for (size_t i = 0; i < 4; ++i)
        if (def->Armor.Slots.front() == names[i])
        {
          inv->EquipArmor(i, entry, *items);
          return;
        }
    }
    inv->QuickMove(slot);
    return;
  }
  auto selection = entry;
  if (half && selection.count > 0)
    selection.count = (selection.count + 1) / 2;
  BeginDragFromSlot(address, selection);
}
void UGameSession::OpenPlayerWindow(int section)
{
  if (Application)
    Application->OpenPlayerWindow(section);
}
void UGameSession::DeleteCreativeAssignment()
{
  auto *inv = GetControlledInventory(World.get());
  if (!inv || !inv->IsCreative())
    return;
  const size_t row = inv->GetActiveBarIndex(), slot = inv->GetActiveSlotIndex();
  if (!inv->GetSlot(row * 10 + slot).IsCatalogReference())
    return;
  CancelDrag();
  inv->ClearHotbarSlot(row, slot);
}
bool UGameSession::PickBlockUnderCrosshair()
{
  if (!World || World->GetGameMode() != WorldGameMode::Creative)
    return false;
  auto *actor = World->GetControlledCreature();
  auto camera = World->GetCurrentUserCamera();
  if (!actor || !camera || actor->CarryHandCount)
    return false;
  auto hit = RaycastSolidBlocks(
      World->GetBlockWorld(), World->GetBlockRegistry(), camera->GetPosition(),
      camera->GetFront(), 6.f, BlockRaycastTargetMode::Pointable);
  if (!hit)
    return false;
  const auto id = World->GetBlockRegistry().GetTypeNameById(
      World->GetBlockWorld().GetBlock(hit->blockPos));
  auto &inv = actor->GetInventory();
  for (size_t i = 0; i < 40; ++i)
    if (!inv.GetSlot(i).empty && inv.GetSlot(i).Id == id &&
        inv.GetSlot(i).kind == InventoryEntryKind::Block)
    {
      inv.SetActiveSlot(i / 10, i % 10);
      return true;
    }
  InventoryEntryRef ref;
  ref.empty = false;
  ref.kind = InventoryEntryKind::Block;
  ref.Id = id;
  ref.count = 1;
  ref.form = InventoryEntryForm::CatalogReference;
  auto assign = [&](size_t i)
  {
    inv.AssignToHotbar(i / 10, i % 10, ref);
    inv.SetActiveSlot(i / 10, i % 10);
    return true;
  };
  const size_t active = inv.GetActiveBarIndex() * 10 + inv.GetActiveSlotIndex();
  if (inv.GetSlot(active).empty || inv.GetSlot(active).IsCatalogReference())
    return assign(active);
  for (size_t i = 0; i < 40; ++i)
    if (inv.GetSlot(i).empty)
      return assign(i);
  if (Application)
    Application->ShowInteractionMessage(
        "Inventory full: free a slot to pick this block", 2.f);
  return false;
}
void UGameSession::DiscardInventory()
{
  auto *inv = GetControlledInventory(World.get());
  auto *actor = World ? World->GetControlledCreature() : nullptr;
  if (!inv || !actor)
    return;
  SlotSurface surface = SlotSurface::Hotbar;
  size_t index = inv->GetActiveBarIndex() * 10 + inv->GetActiveSlotIndex();
  int count = 1;
  if (Drag.Active)
  {
    if (!UInventoryOperations::Matches(*inv, Drag.sourceRevision,
                                       Drag.sourceCreative) ||
        Drag.sourceOwner != actor->GetId())
    {
      CancelDrag();
      return;
    }
    surface = Drag.source.surface;
    index = surface == SlotSurface::Hotbar
                ? Drag.source.bar * 10 + Drag.source.slot
                : Drag.source.slot;
    count = std::max(1, Drag.entry.count);
  }
  InventoryEntryRef actual;
  if (surface == SlotSurface::Hotbar)
    actual = inv->GetSlot(index);
  else if (surface == SlotSurface::CharacterArmor)
    actual = inv->GetEquippedArmor(index);
  else if (surface == SlotSurface::CharacterOffhand)
    actual = inv->GetEquippedOffhand();
  else
    return;
  if (actual.empty || (actual.kind != InventoryEntryKind::Item &&
                       actual.kind != InventoryEntryKind::Block))
    return;
  auto &physics = UWorldItemSystem::Instance();
  if (!physics.CanActivate() || !physics.ReserveDrops(1))
  {
    if (Application)
      Application->ShowInteractionMessage("Physical body budget is busy");
    return;
  }
  std::string reason;
  auto publish = [&](const InventoryEntryRef &out)
  { return physics.DropFromActor(*World, *actor, out, reason); };
  const auto result =
      surface == SlotSurface::Hotbar
          ? UInventoryOperations::TransferToWorld(*inv, index, count, publish)
          : UInventoryOperations::TransferEquipmentToWorld(
                *inv, surface == SlotSurface::CharacterOffhand, index, publish);
  if (result)
    CancelDrag();
  else if (Application)
    Application->ShowInteractionMessage(reason.empty() ? result.reason
                                                       : reason);
}
void UGameSession::SortInventory()
{
  CancelDrag();
  if (auto *inv = GetControlledInventory(World.get()))
    inv->Sort();
}
void UGameSession::RecoverInventory(size_t index)
{
  CancelDrag();
  if (auto *inv = GetControlledInventory(World.get()))
  {
    auto recover = [&](size_t slot)
    {
      if (slot >= inv->GetRecovery().size())
        return;
      const auto &entry = inv->GetRecovery()[slot];
      if (entry.kind == InventoryEntryKind::Block &&
          World->GetBlockRegistry().GetPackBlockIdByTypeName(entry.Id) ==
              BLOCK_AIR)
      {
        if (Application)
          Application->ShowInteractionMessage(
              "Restore the missing resource pack before recovering this item");
        return;
      }
      const auto result = inv->Recover(slot);
      if (!result && Application)
        Application->ShowInteractionMessage(result.reason);
    };
    if (index != static_cast<size_t>(-1))
    {
      recover(index);
      return;
    }
    for (size_t i = inv->GetRecovery().size(); i-- > 0;)
      recover(i);
  }
}

std::vector<InventoryGroupView> UGameSession::GetGroups(ContentKind tab) const
{
  return GetGroups(tab, GetInventoryMode());
}

std::vector<InventoryGroupView>
UGameSession::GetGroups(ContentKind tab, InventoryMode mode) const
{
  std::vector<InventoryGroupView> groups;
  const auto ids = ContentCatalog.GetTypeIds(tab);
  for (const std::string &Id : ids)
  {
    const auto entries = ContentCatalog.GetEntries(tab, Id);
    if (mode == InventoryMode::Owned && entries.empty())
    {
      continue;
    }
    groups.push_back({Id, ContentCatalog.GetTypeDisplayName(Id), 0});
  }
  std::stable_sort(groups.begin(), groups.end(),
                   [](const InventoryGroupView &a, const InventoryGroupView &b)
                   {
                     if (a.Id == "misc")
                       return false;
                     if (b.Id == "misc")
                       return true;
                     return a.label < b.label;
                   });
  return groups;
}

std::vector<InventoryEntryView>
UGameSession::GetEntries(ContentKind tab, const std::string &groupId) const
{
  return GetEntries(tab, groupId, GetInventoryMode());
}

size_t UGameSession::GetInventorySignature() const
{
  const auto *inv = GetControlledInventory(World.get());
  if (!inv)
    return 0;
  size_t signature = inv->IsCreative() ? 1 : 0;
  auto mix = [&](size_t value)
  { signature ^= value + 0x9e3779b9u + (signature << 6) + (signature >> 2); };
  for (const auto &row : inv->GetHotbars())
    for (const auto &slot : row.slots)
    {
      const auto &e = slot.entry;
      mix(e.instanceId);
      mix(static_cast<size_t>(e.form));
      mix(std::hash<std::string>{}(e.Id));
      mix(static_cast<size_t>(e.count));
      mix(std::hash<float>{}(e.wear));
      mix(e.broken);
      mix(static_cast<size_t>(e.contentsUnits));
    }
  return signature;
}

std::vector<InventoryEntryView>
UGameSession::GetEntries(ContentKind tab, const std::string &groupId,
                         InventoryMode mode) const
{
  std::vector<InventoryEntryView> result;
  auto entries = ContentCatalog.GetEntries(tab, groupId);
  const UCreatureInventory *creatureInv = GetControlledInventory(World.get());
  const std::map<std::string, int> *inv =
      creatureInv ? &creatureInv->GetStorage() : nullptr;
  for (const auto &e : entries)
  {
    InventoryEntryRef ref;
    switch (tab)
    {
    case ContentKind::Block:
      ref.kind = InventoryEntryKind::Block;
      break;
    case ContentKind::Object:
      ref.kind = InventoryEntryKind::Object;
      break;
    case ContentKind::UCreature:
      ref.kind = InventoryEntryKind::UCreature;
      break;
    case ContentKind::Skin:
      ref.kind = InventoryEntryKind::Skin;
      break;
    case ContentKind::Item:
      ref.kind = InventoryEntryKind::Item;
      break;
    }
    ref.Id = e.Id;
    ref.empty = false;
    if (tab == ContentKind::UCreature || tab == ContentKind::Skin)
    {
      ref.count = 1;
      ref.form = InventoryEntryForm::CatalogReference;
      result.push_back({ref, e.displayName});
      continue;
    }
    if (mode == InventoryMode::Owned && creatureInv)
    {
      for (const auto &row : creatureInv->GetHotbars())
        for (const auto &slot : row.slots)
          if (!slot.empty && !slot.entry.empty && slot.entry.kind == ref.kind &&
              slot.entry.Id == e.Id)
            result.push_back({slot.entry, e.displayName});
      continue;
    }
    if (mode == InventoryMode::Creative)
    {
      ref.form = InventoryEntryForm::CatalogReference;
      ref.count = 1;
      result.push_back({ref, e.displayName});
      continue;
    }
    ref.count = 0;
    if (inv)
    {
      const auto it = inv->find(e.Id);
      if (it != inv->end())
      {
        ref.count = it->second;
      }
    }
    if (mode == InventoryMode::Owned && ref.count == 0)
    {
      continue;
    }
    result.push_back({ref, e.displayName});
  }
  std::sort(result.begin(), result.end(),
            [](const InventoryEntryView &a, const InventoryEntryView &b)
            {
              if (a.label == b.label)
                return a.ref.Id < b.ref.Id;
              return a.label < b.label;
            });
  return result;
}

bool UGameSession::CanAssignToHotbar(const InventoryEntryRef &entry,
                                     size_t barIndex, size_t slotIndex) const
{
  if (entry.empty || entry.Id.empty() || slotIndex >= 10)
  {
    return false;
  }
  const UCreatureInventory *creatureInv = GetControlledInventory(World.get());
  if (!creatureInv)
  {
    return false;
  }
  const_cast<UCreatureInventory *>(creatureInv)
      ->EnsureHotbarCount(static_cast<size_t>(GetHotbarCountSetting()));
  if (barIndex >= creatureInv->GetHotbarCount())
  {
    return false;
  }

  // Palette/hotbar validation uses derived inventory mode.
  const InventoryMode inventoryMode = GetInventoryMode();
  if (!World)
  {
    return false;
  }

  switch (entry.kind)
  {
  case InventoryEntryKind::Block:
    if (World->GetBlockRegistry().GetPackBlockIdByTypeName(entry.Id) ==
        BLOCK_AIR)
    {
      std::cerr
          << "GameSession: block not in registry, hotbar assign rejected: "
          << entry.Id << std::endl;
      return false;
    }
    if (inventoryMode == InventoryMode::Owned)
    {
      const auto &storage = creatureInv->GetStorage();
      const auto it = storage.find(entry.Id);
      return it != storage.end() && it->second != 0;
    }
    return true;

  case InventoryEntryKind::Item:
  {
    const UItemDefinitionStorage *items = World->GetItemDefinitionStorage();
    if (!items || items->Get(entry.Id) == nullptr)
    {
      return false;
    }
    if (inventoryMode == InventoryMode::Owned)
    {
      const auto &storage = creatureInv->GetStorage();
      const auto it = storage.find(entry.Id);
      return it != storage.end() && it->second != 0;
    }
    return true;
  }

  case InventoryEntryKind::Object:
  {
    bool inCatalog = false;
    for (const auto &typeId : ContentCatalog.GetTypeIds(ContentKind::Object))
    {
      for (const auto &e :
           ContentCatalog.GetEntries(ContentKind::Object, typeId))
      {
        if (e.Id == entry.Id)
        {
          inCatalog = true;
          break;
        }
      }
      if (inCatalog)
      {
        break;
      }
    }
    if (!inCatalog)
    {
      return false;
    }
    if (inventoryMode == InventoryMode::Owned)
    {
      const auto &storage = creatureInv->GetStorage();
      const auto it = storage.find(entry.Id);
      return it != storage.end() && it->second != 0;
    }
    return true;
  }

  case InventoryEntryKind::UCreature:
  case InventoryEntryKind::Skin:
  {
    if (inventoryMode == InventoryMode::Owned &&
        !ModePolicy::AllowsMobSpawnFromPalette(GetWorldGameMode()))
    {
      return false;
    }
    const ContentKind tab = (entry.kind == InventoryEntryKind::Skin)
                                ? ContentKind::Skin
                                : ContentKind::UCreature;
    for (const auto &typeId : ContentCatalog.GetTypeIds(tab))
    {
      for (const auto &e : ContentCatalog.GetEntries(tab, typeId))
      {
        if (e.Id == entry.Id)
        {
          return true;
        }
      }
    }
    const auto &inv = creatureInv->GetStorage();
    const auto it = inv.find(entry.Id);
    return it != inv.end() && it->second != 0;
  }

  default:
    return false;
  }
}

bool UGameSession::AssignToHotbar(const InventoryEntryRef &entry,
                                  size_t barIndex, size_t slotIndex)
{
  if (!CanAssignToHotbar(entry, barIndex, slotIndex))
  {
    return false;
  }
  return AssignSlot(barIndex, slotIndex, entry);
}

bool UGameSession::CanSpawnCreatureByView(const std::string &speciesId) const
{
  return World && World->CanSpawnCreatureByView(speciesId);
}

std::string
UGameSession::GetCreatureSpawnBlockedHint(const std::string &speciesId) const
{
  if (!World)
  {
    return {};
  }
  return World->GetCreatureSpawnBlockedHint(speciesId);
}

WorldGameMode UGameSession::GetWorldGameMode() const
{
  return World ? World->GetGameMode() : ActiveWorldGameMode;
}
WorldDifficulty UGameSession::GetWorldDifficulty() const
{
  return World ? World->GetDifficulty() : ActiveWorldDifficulty;
}
InventoryMode UGameSession::GetInventoryMode() const
{
  if (CheatCreativeInventory)
  {
    return InventoryMode::Creative;
  }
  if (!World)
  {
    return InventoryMode::Creative;
  }
  return World->GetGameMode() == WorldGameMode::Creative
             ? InventoryMode::Creative
             : InventoryMode::Owned;
}

void UGameSession::SyncToWorldGameMode(WorldGameMode mode)
{
  CancelDrag();
  const WorldGameMode previous = ActiveWorldGameMode;
  ActiveWorldGameMode = mode;
  if (World)
  {
    World->SetGameMode(mode);
    if (auto *inv = GetControlledInventory(World.get()))
      inv->SetCreative(mode == WorldGameMode::Creative);
    World->ApplyGameModeLocomotionPolicy();
    if (mode == WorldGameMode::Survival && previous == WorldGameMode::Creative)
    {
      if (UCreature *creature = World->GetControlledCreature())
      {
        creature->GetInventory().SetCreative(false);
      }
    }
  }
  if (Application && previous != mode &&
      !ModePolicy::AllowsCreativePalette(mode))
  {
    Application->CloseCreativePalette();
  }
}

void UGameSession::SyncToWorldDifficulty(WorldDifficulty difficulty)
{
  if (World &&
      !ModePolicy::AllowsDifficultyCommand(World->GetDifficulty(), difficulty))
  {
    return;
  }
  ActiveWorldDifficulty = difficulty;
  if (World)
  {
    World->SetDifficulty(difficulty);
  }
}

bool UGameSession::ApplyHumanAppearance(const HumanAppearance &value,
                                        const std::string &skin)
{
  if (!World)
    return false;
  auto *actor = World->GetControlledCreature();
  auto user = World->GetCurrentUser();
  if (!actor || !user || actor->GetTypeId() != "human")
    return false;
  if (!skin.empty())
  {
    auto defs = World->GetSkinDefinitionStorage();
    const auto *def = defs ? defs->Get(skin) : nullptr;
    if (!def || def->creatureId != "human")
      return false;
  }
  actor->SetHumanAppearance(value);
  actor->SetSkinId(skin);
  user->SetHumanAppearance(value);
  user->SetSelectedSkinId(skin);
  user->SetSelectedAppearanceTypeId(skin);
  return true;
}

CharacterStatsSnapshot UGameSession::GetCharacterStatsSnapshot() const
{
  CharacterStatsSnapshot snap;
  snap.gameMode = World ? World->GetGameMode() : ActiveWorldGameMode;
  snap.difficulty = World ? World->GetDifficulty() : ActiveWorldDifficulty;
  if (!World)
  {
    return snap;
  }
  const UCreature *creature = World->GetControlledCreature();
  if (!creature)
  {
    return snap;
  }
  snap.valid = true;
  snap.typeId = creature->GetTypeId();
  snap.skinId = creature->GetSkinId();
  if (const auto resolved = World->GetResolvedAppearanceShared(*creature))
    snap.skinId = resolved->effectiveSkinId;
  snap.humanAppearance = creature->GetHumanAppearance();
  snap.vitals = creature->GetVitals();
  snap.attributes = StatusEffectSystem::GetEffectiveAttributes(*creature);

  snap.effects = creature->GetStatusEffects();
  snap.movementMultiplier =
      StatusEffectSystem::GetMoveSpeedMultiplier(*creature);
  snap.protection = creature->GetArmorGroups();
  snap.activeRow = creature->GetInventory().GetActiveBarIndex();
  snap.activeSlot = creature->GetInventory().GetActiveSlotIndex();
  for (size_t i = 0; i < 40; ++i)
    snap.slots[i] = creature->GetInventory().GetSlot(i);
  snap.recovery = creature->GetInventory().GetRecovery();
  // Equipped armor (0..5) + active tool items (2 slots).
  const UCreatureInventory &inv = creature->GetInventory();
  for (size_t i = 0; i < UCreatureInventory::kArmorSlotCount; ++i)
  {
    const InventoryEntryRef &e = inv.GetEquippedArmor(i);
    snap.equippedArmor[i].itemId = e.empty ? std::string() : e.Id;
    snap.equippedArmor[i].wear = e.wear;
    snap.equippedArmor[i].broken = e.broken;
  }
  for (size_t i = 0; i < 2; ++i)
  {
    snap.equippedTools[i] = CharacterStatsSnapshot::EquippedSlotSnapshot{};
  }
  {
    const InventoryEntryRef *active = inv.GetActiveEntryRef();
    if (active && !active->empty && !active->Id.empty() &&
        (active->kind == InventoryEntryKind::Item ||
         active->kind == InventoryEntryKind::Block))
    {
      snap.equippedTools[0].itemId = active->Id;
      snap.equippedTools[0].wear = active->wear;
      snap.equippedTools[0].broken = active->broken;
      snap.equippedTools[0].isBlock = active->kind == InventoryEntryKind::Block;
    }
    const InventoryEntryRef &oh = inv.GetEquippedOffhand();
    if (!oh.empty && !oh.Id.empty())
    {
      snap.equippedTools[1].itemId = oh.Id;
      snap.equippedTools[1].wear = oh.wear;
      snap.equippedTools[1].broken = oh.broken;
      snap.equippedTools[1].isBlock = oh.kind == InventoryEntryKind::Block;
    }
  }

  if (UItemDefinitionStorage *itemDefs = World->GetItemDefinitionStorage())
  {
    for (const auto &kv : inv.GetStorage())
    {
      if (!itemDefs->Get(kv.first) || kv.second == 0)
      {
        continue;
      }
      CharacterStatsSnapshot::CarriedItem carried;
      carried.id = kv.first;
      carried.count = kv.second;
      snap.carriedItems.push_back(std::move(carried));
    }
  }

  if (const CreatureDefinition *def =
          World->GetCreatureDefinition(creature->GetTypeId()))
  {
    snap.displayName = def->displayName;
  }
  else
  {
    snap.displayName = creature->GetTypeId();
  }
  return snap;
}

CommandResult UGameSession::Execute(const std::vector<std::string> &args)
{
  if (args.empty())
  {
    return {false, "Empty"};
  }
  std::string line;
  for (size_t i = 0; i < args.size(); ++i)
  {
    if (i > 0)
    {
      line += ' ';
    }
    line += args[i];
  }
  return UCommandRegistry.ExecuteLine(line);
}

void UGameSession::AddChatLine(const std::string &line)
{
  ChatLog.push_back(line);
  if (ChatLog.size() > 200)
  {
    ChatLog.erase(ChatLog.begin());
  }
}

void UGameSession::InitCommandHistory(const std::filesystem::path &filePath)
{
  CommandHistory.SetFilePath(filePath);
  CommandHistory.Load();
}

void UGameSession::SaveCommandHistory() { CommandHistory.Save(); }

} // namespace cutum
