#include "Game/GameSession.h"
#include "Content/ContentType.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "App/Application.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Render/Camera/Camera.h"
#include "World/Core/World.h"
#include "World/Prefabs/Prefab.h"

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
                                     const UBlockDefinitionStorage &blocks,
                                     const UPrefabLibrary &prefabs)
{
  ContentCatalog.LoadTypes(typesJsonPath);
  ReindexBlockCatalog(blocks, prefabs);
}

void UGameSession::ReindexBlockCatalog(const UBlockDefinitionStorage &blocks,
                                       const UPrefabLibrary &prefabs)
{
  ContentCatalog.IndexBlocks(blocks);
  ContentCatalog.IndexPrefabs(prefabs);
  if (World)
  {
    if (const auto &creatureDefs = World->GetCreatureDefinitionStorage())
    {
      ContentCatalog.IndexCreatures(*creatureDefs);
    }
    if (const auto &skinDefs = World->GetSkinDefinitionStorage())
    {
      ContentCatalog.IndexSkins(*skinDefs);
    }
  }
}

void UGameSession::RegisterCommands()
{
  UCommandRegistry.Register(
      "help",
      [](const std::vector<std::string> &)
      {
        return CommandResult{true, "Commands: help, give, tp, fly, time, "
                                   "spawn, select_skin, apply_skin, possess, "
                                   "depossess, select_appearance"};
      });

  UCommandRegistry.Register(
      "time", [](const std::vector<std::string> &)
      { return CommandResult{true, "Time of day is not implemented yet."}; });

  UCommandRegistry.Register(
      "give",
      [this](const std::vector<std::string> &args)
      {
        if (args.size() < 2)
        {
          return CommandResult{false, "Usage: give <block>"};
        }
        if (UCreatureInventory *inv = GetControlledInventory(World.get()))
        {
          inv->AddToInventory(args[1]);
        }
        else
        {
          return CommandResult{false, "No controlled creature"};
        }
        return CommandResult{true, "Added " + args[1]};
      });

  UCommandRegistry.Register(
      "tp",
      [this](const std::vector<std::string> &args)
      {
        if (args.size() < 4)
        {
          return CommandResult{false, "Usage: tp <x> <y> <z>"};
        }
        auto user = World->GetCurrentUser();
        auto camera = World->GetCurrentUserCamera();
        UCreature *controlled = World->GetControlledCreature();
        if (!user || !camera)
        {
          return CommandResult{false, "No user/camera"};
        }
        try
        {
          const float x = std::stof(args[1]);
          const float y = std::stof(args[2]);
          const float z = std::stof(args[3]);
          const glm::vec3 eye{x, y, z};
          user->SetPosition(eye);
          camera->SetPosition(eye);
          if (controlled)
          {
            controlled->SetBodyOrigin(glm::vec3(
                eye.x, FeetYFromEye(eye, controlled->GetEyeOffset().y), eye.z));
          }
          return CommandResult{true, "Teleported"};
        }
        catch (...)
        {
          return CommandResult{false, "Invalid coordinates"};
        }
      });

  UCommandRegistry.Register(
      "fly",
      [this](const std::vector<std::string> &args)
      {
        auto camera = World->GetCurrentUserCamera();
        if (!camera)
        {
          return CommandResult{false, "No camera"};
        }
        bool enable = true;
        if (args.size() >= 2)
        {
          enable = args[1] == "on" || args[1] == "1" || args[1] == "true";
        }
        camera->SetFreeMove(enable);
        if (UCreature *c = World->GetControlledCreature())
        {
          c->GetLocomotion().SetMode(enable ? CreatureMovementMode::Flying
                                            : CreatureMovementMode::Walking);
        }
        return CommandResult{true, enable ? "Flight on" : "Flight off"};
      });

  UCommandRegistry.Register(
      "spawn",
      [this](const std::vector<std::string> &args)
      {
        if (!World)
        {
          return CommandResult{false, "No world"};
        }
        if (args.size() < 2)
        {
          return CommandResult{false, "Usage: spawn <species> [skin]"};
        }
        const std::string &species = args[1];
        const CreatureDefinition *def = World->GetCreatureDefinition(species);
        if (!def)
        {
          return CommandResult{false, "Unknown species: " + species};
        }
        if (!def->catalog.spawnable)
        {
          return CommandResult{false, "Species is not spawnable: " + species};
        }
        const std::string skin = args.size() >= 3 ? args[2] : "";
        if (!skin.empty())
        {
          const auto &skinStorage = World->GetSkinDefinitionStorage();
          if (!skinStorage || !skinStorage->IsCompatible(skin, species))
          {
            return CommandResult{false, "Skin is not compatible with species"};
          }
        }
        const glm::vec3 eyeOffset(0.0f, def->eyeHeight, 0.0f);
        glm::vec3 bodyOrigin = World->GetSpawnPoint();
        bodyOrigin.y -= eyeOffset.y;
        if (auto camera = World->GetCurrentUserCamera())
        {
          glm::vec3 forward = camera->GetFront();
          forward.y = 0.0f;
          if (glm::length(forward) > 0.01f)
          {
            bodyOrigin = camera->GetPosition() - eyeOffset +
                         glm::normalize(forward) * 3.0f;
          }
          else
          {
            bodyOrigin =
                camera->GetPosition() - eyeOffset + glm::vec3(3.0f, 0.0f, 0.0f);
          }
        }
        const CreatureId Id = World->SpawnCreature(species, bodyOrigin, skin);
        if (Id == 0)
        {
          return CommandResult{false, "Spawn failed"};
        }
        return CommandResult{true, "Spawned " + species +
                                       " Id=" + std::to_string(Id)};
      });

  UCommandRegistry.Register(
      "select_skin",
      [this](const std::vector<std::string> &args)
      {
        if (args.size() < 2)
        {
          return CommandResult{false, "Usage: select_skin <skin_id>"};
        }
        auto user = World->GetCurrentUser();
        UCreature *controlled = World->GetControlledCreature();
        if (!user || !controlled)
        {
          return CommandResult{false, "No controlled creature"};
        }
        std::string error;
        if (!World->TryApplySkin(controlled->GetId(), args[1], &error))
        {
          return CommandResult{false, error};
        }
        user->SetSelectedSkinId(args[1]);
        return CommandResult{true, "Skin set to " + args[1]};
      });

  UCommandRegistry.Register(
      "apply_skin",
      [this](const std::vector<std::string> &args)
      {
        if (!World)
        {
          return CommandResult{false, "No world"};
        }
        if (args.size() < 2)
        {
          return CommandResult{false, "Usage: apply_skin <skin_id>"};
        }
        auto camera = World->GetCurrentUserCamera();
        if (!camera)
        {
          return CommandResult{false, "No camera"};
        }
        const auto target = World->PickCreatureByView(camera->GetPosition(),
                                                      camera->GetFront(), 8.0f);
        if (!target)
        {
          return CommandResult{false, "No creature in view"};
        }
        std::string error;
        if (!World->TryApplySkin(*target, args[1], &error))
        {
          return CommandResult{false, error};
        }
        return CommandResult{true, "Applied skin " + args[1]};
      });

  UCommandRegistry.Register(
      "possess",
      [this](const std::vector<std::string> &args)
      {
        if (!World)
        {
          return CommandResult{false, "No world"};
        }
        CreatureId target = 0;
        if (args.size() >= 2)
        {
          try
          {
            target = static_cast<CreatureId>(std::stoull(args[1]));
          }
          catch (...)
          {
            return CommandResult{false, "Invalid creature Id"};
          }
        }
        else
        {
          World->ForEachCreature(
              [&](UCreature &c)
              {
                if (target == 0 && !c.IsPlayerCharacter())
                {
                  target = c.GetId();
                }
              });
        }
        if (target == 0 || !World->SetControlledCreature(target))
        {
          return CommandResult{false, "Cannot possess"};
        }
        if (auto cam = World->GetCurrentUserCamera())
        {
          if (UCreature *c = World->GetCreature(target))
          {
            cam->SetPosition(c->GetEyePosition());
            cam->SetOrientation(CameraYawFromModelYaw(c->GetYaw()),
                                c->GetPitch());
          }
        }
        return CommandResult{true, "Possessing Id=" + std::to_string(target)};
      });

  UCommandRegistry.Register(
      "depossess",
      [this](const std::vector<std::string> &)
      {
        if (!World)
        {
          return CommandResult{false, "No world"};
        }
        const CreatureId playerId = World->GetPlayerCreatureId();
        if (playerId == 0 || !World->SetControlledCreature(playerId))
        {
          return CommandResult{false, "No player creature"};
        }
        if (auto cam = World->GetCurrentUserCamera())
        {
          if (UCreature *c = World->GetPlayerCreature())
          {
            cam->SetPosition(c->GetEyePosition());
            cam->SetOrientation(CameraYawFromModelYaw(c->GetYaw()),
                                c->GetPitch());
          }
        }
        return CommandResult{true, "Returned to player"};
      });

  UCommandRegistry.Register(
      "select_appearance",
      [this](const std::vector<std::string> &args)
      {
        if (args.size() < 2)
        {
          return CommandResult{
              false, "Usage: select_appearance <skin_id> (alias: select_skin)"};
        }
        auto user = World->GetCurrentUser();
        UCreature *controlled = World->GetControlledCreature();
        if (!user || !controlled)
        {
          return CommandResult{false, "No controlled creature"};
        }
        std::string error;
        if (!World->TryApplySkin(controlled->GetId(), args[1], &error))
        {
          return CommandResult{false, error};
        }
        user->SetSelectedSkinId(args[1]);
        user->SetSelectedAppearanceTypeId(args[1]);
        return CommandResult{true, "Appearance set to " + args[1]};
      });
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
    if (!slot.entry.Id.empty())
    {
      slots[i].Id = slot.entry.Id;
      slots[i].label = HumanizeBlockName(slot.entry.Id);
      slots[i].entryKind = slot.entry.kind;
      slots[i].isBlock = (slot.entry.kind == InventoryEntryKind::Block);
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
    case ContentKind::UObject:
      ref.kind = InventoryEntryKind::UObject;
      break;
    case ContentKind::UCreature:
      ref.kind = InventoryEntryKind::UCreature;
      break;
    case ContentKind::Skin:
      ref.kind = InventoryEntryKind::Skin;
      break;
    }
    ref.Id = e.Id;
    ref.empty = false;
    if (tab == ContentKind::UCreature || tab == ContentKind::Skin)
    {
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
        std::cerr << "GameSession: block not in registry, hotbar assign rejected: "
                  << entry.Id << std::endl;
        return false;
      }
    }
    else if (entry.kind == InventoryEntryKind::UObject)
    {
      const auto entries =
          ContentCatalog.GetEntries(ContentKind::UObject, entry.Id);
      if (entries.empty())
      {
        std::cerr << "GameSession: prefab not in catalog, hotbar assign rejected: "
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
  const_cast<UCreatureInventory *>(creatureInv)->EnsureHotbarCount(
      static_cast<size_t>(GetHotbarCountSetting()));
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

std::string UGameSession::GetCreatureSpawnBlockedHint(
    const std::string &speciesId) const
{
  if (!World)
  {
    return {};
  }
  return World->GetCreatureSpawnBlockedHint(speciesId);
}

InventoryMode UGameSession::GetInventoryMode() const { return ActiveInventoryMode; }

void UGameSession::SetInventoryMode(InventoryMode mode)
{
  ActiveInventoryMode = mode;
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
