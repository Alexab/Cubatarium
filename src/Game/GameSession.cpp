#include "Game/GameSession.h"
#include "Game/Interfaces/IUGameContent.h"
#include "App/Application.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Commands/WorldCommands.h"
#include "Content/ContentType.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Render/Camera/Camera.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "World/Core/World.h"
#include "World/Objects/ObjectLibrary.h"
#include "WorldGen/Core/WorldGenContentReload.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace cutum
{

namespace
{

UCreatureInventory *GetControlledInventory(UWorld *world)
{
  if (!world)
  {
    return nullptr;
  }
  if (UCreature *creature = world->GetControlledCreature())
  {
    return &creature->GetInventory();
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
  return inv->GetHotbarCount();
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
  return inv->AssignToHotbar(barIndex, slotIndex, entry);
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
  SelectSlot(0, static_cast<size_t>(slotIndex));
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

void UGameSession::BeginDragFromSlot(const SlotAddress &source,
                                     const InventoryEntryRef &entry)
{
  if (entry.empty)
  {
    return;
  }
  Drag.Active = true;
  Drag.entry = entry;
  Drag.source = source;
}

bool UGameSession::IsDragging() const { return Drag.Active; }

void UGameSession::CancelDrag() { Drag = DragState{}; }

bool UGameSession::DropOnSlot(const SlotAddress &target)
{
  if (!Drag.Active || target.surface == SlotSurface::None)
  {
    return false;
  }
  if (SameSlotAddress(target, Drag.source))
  {
    CancelDrag();
    return true;
  }

  const SlotAddress source = Drag.source;
  const InventoryEntryRef entry = Drag.entry;

  if (target.surface == SlotSurface::Hotbar)
  {
    if (!CanAssignToHotbar(entry, target.bar, target.slot))
    {
      return false;
    }
    if (!AssignSlot(target.bar, target.slot, entry))
    {
      return false;
    }
    if (source.surface == SlotSurface::Hotbar &&
        (source.bar != target.bar || source.slot != target.slot))
    {
      if (UCreatureInventory *inv = GetControlledInventory(World.get()))
      {
        inv->ClearHotbarSlot(source.bar, source.slot);
      }
    }
    SelectSlot(target.bar, target.slot);
    CancelDrag();
    return true;
  }

  if (target.surface == SlotSurface::PaletteGrid &&
      source.surface == SlotSurface::Hotbar)
  {
    if (UCreatureInventory *inv = GetControlledInventory(World.get()))
    {
      inv->ClearHotbarSlot(source.bar, source.slot);
    }
    CancelDrag();
    return true;
  }

  return false;
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
    if (tab == ContentKind::UCreature || tab == ContentKind::Skin ||
        tab == ContentKind::Item)
    {
      ref.count = (tab == ContentKind::Item) ? 1 : 1;
      if (tab == ContentKind::Item)
      {
        ref.wear = 0.f;
        ref.broken = false;
      }
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
    if (mode == InventoryMode::Owned && ref.count <= 0)
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
  if (entry.empty || slotIndex >= 10)
  {
    return false;
  }
  if (World)
  {
    if (entry.kind == InventoryEntryKind::Block)
    {
      if (World->GetBlockRegistry().GetIdByTypeName(entry.Id) == BLOCK_AIR)
      {
        std::cerr
            << "GameSession: block not in registry, hotbar assign rejected: "
            << entry.Id << std::endl;
        return false;
      }
    }
    else if (entry.kind == InventoryEntryKind::Object)
    {
      const auto entries =
          ContentCatalog.GetEntries(ContentKind::Object, entry.Id);
      if (entries.empty())
      {
        std::cerr
            << "GameSession: prefab not in catalog, hotbar assign rejected: "
            << entry.Id << std::endl;
        return false;
      }
    }
    else if (entry.kind == InventoryEntryKind::Item)
    {
      bool found = false;
      for (const auto &typeId : ContentCatalog.GetTypeIds(ContentKind::Item))
      {
        for (const auto &e :
             ContentCatalog.GetEntries(ContentKind::Item, typeId))
        {
          if (e.Id == entry.Id)
          {
            found = true;
            break;
          }
        }
        if (found)
        {
          break;
        }
      }
      if (!found)
      {
        std::cerr
            << "GameSession: item not in catalog, hotbar assign rejected: "
            << entry.Id << std::endl;
        return false;
      }
    }
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
  if (ActiveInventoryMode == InventoryMode::Creative)
  {
    return true;
  }
  const auto &inv = creatureInv->GetStorage();
  const auto it = inv.find(entry.Id);
  return it != inv.end() && it->second > 0;
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

InventoryMode UGameSession::GetInventoryMode() const
{
  return ActiveInventoryMode;
}

void UGameSession::SetInventoryMode(InventoryMode mode)
{
  ActiveInventoryMode = mode;
}

void UGameSession::SyncToWorldGameMode(WorldGameMode mode)
{
  ActiveWorldGameMode = mode;
  if (World)
  {
    World->SetGameMode(mode);
    World->ApplyGameModeLocomotionPolicy();
  }
  SetInventoryMode(mode == WorldGameMode::Survival ? InventoryMode::Owned
                                                   : InventoryMode::Creative);
}

void UGameSession::SyncToWorldDifficulty(WorldDifficulty difficulty)
{
  ActiveWorldDifficulty = difficulty;
  if (World)
  {
    World->SetDifficulty(difficulty);
  }
}

CharacterStatsSnapshot UGameSession::GetCharacterStatsSnapshot() const
{
  CharacterStatsSnapshot snap;
  snap.gameMode = ActiveWorldGameMode;
  snap.difficulty = ActiveWorldDifficulty;
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
  snap.vitals = creature->GetVitals();
  snap.attributes = creature->GetAttributes();

  // Equipped armor (0..5) + active tool items (2 slots).
  const UCreatureInventory &inv = creature->GetInventory();
  for (size_t i = 0; i < 6; ++i)
  {
    const InventoryEntryRef &e = inv.GetEquippedArmor(i);
    snap.equippedArmor[i].itemId = e.empty ? std::string() : e.Id;
    snap.equippedArmor[i].wear = e.wear;
    snap.equippedArmor[i].broken = e.broken;
  }
  for (size_t i = 0; i < 2; ++i)
  {
    const size_t barIndex = i;
    if (barIndex >= inv.GetHotbarCount())
    {
      snap.equippedTools[i] = CharacterStatsSnapshot::EquippedSlotSnapshot{};
      continue;
    }
    size_t selectedSlot = inv.GetActiveSlotIndex(barIndex);
    if (selectedSlot >= 10)
    {
      selectedSlot = 0;
    }
    const HotbarBar &bar = inv.GetHotbar(barIndex);
    const HotbarSlot &slot = bar.slots[selectedSlot];
    if (!slot.empty && slot.entry.kind == InventoryEntryKind::Item &&
        !slot.entry.Id.empty())
    {
      snap.equippedTools[i].itemId = slot.entry.Id;
      snap.equippedTools[i].wear = slot.entry.wear;
      snap.equippedTools[i].broken = slot.entry.broken;
    }
    else
    {
      snap.equippedTools[i] = CharacterStatsSnapshot::EquippedSlotSnapshot{};
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
