#include "Creatures/Core/CreatureInventory.h"
#include "Items/ItemDefinitionStorage.h"
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{
constexpr size_t kHotbarSlots = 10;

constexpr size_t kArmorSlots = 6;

const char *ArmorSlotId(size_t slot)
{
  static constexpr const char *ids[kArmorSlots] = {"head",  "chest", "arms",
                                                    "hands", "legs",  "feet"};
  if (slot >= kArmorSlots)
  {
    return "";
  }
  return ids[slot];
}

void RemapLegacyHotbarEntryId(InventoryEntryRef &entry)
{
  if (entry.kind != InventoryEntryKind::UCreature)
  {
    return;
  }
  if (entry.Id == "test_mob" || entry.Id == "scout")
  {
    entry.Id = "sheep";
  }
  else if (entry.Id == "brute")
  {
    entry.Id = "sand_monster";
  }
  else if (entry.Id == "drifter")
  {
    entry.Id = "wolf";
  }
}
}

void UCreatureInventory::AddItem(const std::string &Id, int count)
{
  Storage[Id] += count;
}

void UCreatureInventory::AddToInventory(const std::string &Id)
{
  if (Storage[Id] < 0)
  {
    return;
  }
  Storage[Id]++;
}

void UCreatureInventory::InitCreativeDefaults()
{
  static const char *kBlocks[] = {
      "wood",      "grass",      "stone", "tree_birch", "pumpkin",
      "sandstone", "stonebrick", "tnt",   "brick",      "bedrock",
      "water",     "lava",       "fire"};
  for (const char *Id : kBlocks)
  {
    Storage[Id] = -1;
  }
}

void UCreatureInventory::EnsureDefaultHotbar()
{
  EnsureHotbarCount(1);
  bool hotbarEmpty = true;
  for (const auto &slot : GetHotbar(0).slots)
  {
    if (!slot.empty && !slot.entry.Id.empty())
    {
      hotbarEmpty = false;
      break;
    }
  }
  if (GetHotbar(0).slots[1].empty)
  {
    bool hasBlock = false;
    for (const auto &slot : GetHotbar(0).slots)
    {
      if (!slot.empty && slot.entry.kind == InventoryEntryKind::Block &&
          !slot.entry.Id.empty())
      {
        hasBlock = true;
        break;
      }
    }
    if (!hasBlock)
    {
      InventoryEntryRef wood;
      wood.kind = InventoryEntryKind::Block;
      wood.Id = "wood";
      wood.empty = false;
      wood.count = -1;
      AssignToHotbar(0, 1, wood);
    }
  }
  if (hotbarEmpty)
  {
    SetActiveSlot(0, 1);
  }
}

bool UCreatureInventory::IsPrimaryHotbarEmpty() const
{
  if (Hotbars.empty())
  {
    return true;
  }
  for (const auto &slot : GetHotbar(0).slots)
  {
    if (!slot.empty && !slot.entry.Id.empty())
    {
      return false;
    }
  }
  return true;
}

void UCreatureInventory::SetObjectHotbar(
    const std::vector<std::string> &object_names)
{
  EnsureHotbarCount(2);
  size_t idx = 0;
  for (const std::string &Name : object_names)
  {
    if (idx >= kHotbarSlots)
    {
      break;
    }
    InventoryEntryRef entry;
    entry.kind = InventoryEntryKind::Object;
    entry.Id = Name;
    entry.empty = false;
    entry.count = 0;
    AssignToHotbar(1, idx, entry);
    ++idx;
  }
}

const HotbarBar &UCreatureInventory::GetHotbar(size_t bar) const
{
  static const HotbarBar kEmpty{};
  if (bar >= Hotbars.size())
  {
    return kEmpty;
  }
  return Hotbars[bar];
}

void UCreatureInventory::ClearHotbarSlot(size_t bar, size_t slot)
{
  if (bar >= Hotbars.size() || slot >= kHotbarSlots)
  {
    return;
  }
  Hotbars[bar].slots[slot] = HotbarSlot{};
}

bool UCreatureInventory::SetActiveSlot(size_t bar, size_t slot)
{
  if (bar >= Hotbars.size() || slot >= kHotbarSlots)
  {
    return false;
  }
  ActiveBarIndex = bar;
  ActiveSlotIndex = slot;
  return true;
}

size_t UCreatureInventory::GetActiveSlotIndex(size_t bar) const
{
  if (bar == ActiveBarIndex)
  {
    return ActiveSlotIndex;
  }
  return kHotbarSlots;
}

const std::string &UCreatureInventory::GetActiveBlockTypeName() const
{
  static const std::string kEmpty;
  const InventoryEntryRef *Active = GetActiveEntryRef();
  if (Active && !Active->empty && Active->kind == InventoryEntryKind::Block)
  {
    return Active->Id;
  }
  return kEmpty;
}

const std::string &UCreatureInventory::GetActiveObjectName() const
{
  static const std::string kEmpty;
  const InventoryEntryRef *Active = GetActiveEntryRef();
  if (Active && !Active->empty && Active->kind == InventoryEntryKind::Object)
  {
    return Active->Id;
  }
  return kEmpty;
}

const InventoryEntryRef *UCreatureInventory::GetActiveEntryRef() const
{
  if (Hotbars.empty() || ActiveBarIndex >= Hotbars.size())
  {
    return nullptr;
  }
  const auto &bar = Hotbars[ActiveBarIndex];
  if (ActiveSlotIndex >= bar.slots.size())
  {
    return nullptr;
  }
  const auto &slot = bar.slots[ActiveSlotIndex];
  if (slot.empty)
  {
    return nullptr;
  }
  return &slot.entry;
}

InventoryEntryRef *UCreatureInventory::GetActiveEntryRef()
{
  return const_cast<InventoryEntryRef *>(
      static_cast<const UCreatureInventory *>(this)->GetActiveEntryRef());
}

const InventoryEntryRef &UCreatureInventory::GetEquippedArmor(size_t slot) const
{
  static InventoryEntryRef kEmpty;
  if (slot >= kArmorSlots)
  {
    return kEmpty;
  }
  return EquippedArmor[slot];
}

bool UCreatureInventory::EquipArmor(size_t slot, const InventoryEntryRef &entry,
                                     const UItemDefinitionStorage &items)
{
  if (slot >= kArmorSlots)
  {
    return false;
  }
  if (entry.empty || entry.kind != InventoryEntryKind::Item || entry.Id.empty())
  {
    return false;
  }

  const ItemDefinition *def = items.Get(entry.Id);
  if (!def || def->Armor.ArmorGroups.empty() || def->Armor.Slots.empty())
  {
    return false;
  }

  const char *slotId = ArmorSlotId(slot);
  bool slotAllowed = false;
  for (const std::string &s : def->Armor.Slots)
  {
    if (s == slotId)
    {
      slotAllowed = true;
      break;
    }
  }
  if (!slotAllowed)
  {
    return false;
  }

  EquippedArmor[slot] = entry;

  // Recompute pre-aggregated armor groups.
  EquippedArmorGroups.Ratings.clear();
  for (size_t i = 0; i < kArmorSlots; ++i)
  {
    const InventoryEntryRef &e = EquippedArmor[i];
    if (e.empty || e.kind != InventoryEntryKind::Item || e.Id.empty())
    {
      continue;
    }
    if (e.broken)
    {
      continue;
    }
    const ItemDefinition *ed = items.Get(e.Id);
    if (!ed || ed->Armor.ArmorGroups.empty())
    {
      continue;
    }
    const char *sid = ArmorSlotId(i);
    if (!ed->Armor.Slots.empty())
    {
      bool allowed = false;
      for (const std::string &s : ed->Armor.Slots)
      {
        if (s == sid)
        {
          allowed = true;
          break;
        }
      }
      if (!allowed)
      {
        continue;
      }
    }
    for (const auto &pair : ed->Armor.ArmorGroups)
    {
      EquippedArmorGroups.Ratings[pair.first] += pair.second;
    }
  }
  return true;
}

void UCreatureInventory::UnequipArmor(size_t slot,
                                       const UItemDefinitionStorage &items)
{
  if (slot >= kArmorSlots)
  {
    return;
  }
  EquippedArmor[slot] = InventoryEntryRef{};
  EquippedArmorGroups.Ratings.clear();
  for (size_t i = 0; i < kArmorSlots; ++i)
  {
    const InventoryEntryRef &e = EquippedArmor[i];
    if (e.empty || e.kind != InventoryEntryKind::Item || e.Id.empty())
    {
      continue;
    }
    if (e.broken)
    {
      continue;
    }
    const ItemDefinition *ed = items.Get(e.Id);
    if (!ed || ed->Armor.ArmorGroups.empty())
    {
      continue;
    }
    const char *sid = ArmorSlotId(i);
    if (!ed->Armor.Slots.empty())
    {
      bool allowed = false;
      for (const std::string &s : ed->Armor.Slots)
      {
        if (s == sid)
        {
          allowed = true;
          break;
        }
      }
      if (!allowed)
      {
        continue;
      }
    }
    for (const auto &pair : ed->Armor.ArmorGroups)
    {
      EquippedArmorGroups.Ratings[pair.first] += pair.second;
    }
  }
}

const InventoryEntryRef &UCreatureInventory::GetEquippedOffhand() const
{
  return EquippedOffhand;
}

bool UCreatureInventory::EquipOffhand(const InventoryEntryRef &entry)
{
  if (entry.empty || entry.Id.empty())
  {
    return false;
  }
  if (entry.kind != InventoryEntryKind::Item &&
      entry.kind != InventoryEntryKind::Block)
  {
    return false;
  }
  EquippedOffhand = entry;
  EquippedOffhand.empty = false;
  return true;
}

void UCreatureInventory::UnequipOffhand()
{
  EquippedOffhand = InventoryEntryRef{};
}

void UCreatureInventory::EnsureHotbarCount(size_t count)
{
  if (Hotbars.size() >= count)
  {
    return;
  }
  while (Hotbars.size() < count)
  {
    Hotbars.push_back(HotbarBar{});
  }
}

bool UCreatureInventory::AssignToHotbar(size_t bar, size_t slot,
                                        const InventoryEntryRef &entry)
{
  EnsureHotbarCount(bar + 1);
  if (slot >= kHotbarSlots)
  {
    return false;
  }
  Hotbars[bar].slots[slot].empty = entry.empty;
  Hotbars[bar].slots[slot].entry = entry;
  return true;
}

void UCreatureInventory::SerializeToJson(nlohmann::json &out) const
{
  out["storage"] = Storage;
  nlohmann::json bars = nlohmann::json::array();
  for (const auto &bar : Hotbars)
  {
    nlohmann::json slots = nlohmann::json::array();
    for (const auto &slot : bar.slots)
    {
      nlohmann::json s;
      s["empty"] = slot.empty;
      if (!slot.empty)
      {
        switch (slot.entry.kind)
        {
        case InventoryEntryKind::Block:
          s["kind"] = "block";
          break;
        case InventoryEntryKind::Object:
          s["kind"] = "object";
          break;
        case InventoryEntryKind::UCreature:
          s["kind"] = "creature";
          break;
        case InventoryEntryKind::Skin:
          s["kind"] = "skin";
          break;
        case InventoryEntryKind::Item:
          s["kind"] = "item";
          break;
        }
        s["id"] = slot.entry.Id;
        s["count"] = slot.entry.count;
        if (slot.entry.kind == InventoryEntryKind::Item)
        {
          s["wear"] = slot.entry.wear;
          s["broken"] = slot.entry.broken;
        }
      }
      slots.push_back(s);
    }
    bars.push_back(slots);
  }
  out["hotbars"] = bars;
  out["active_bar"] = ActiveBarIndex;
  out["active_slot"] = ActiveSlotIndex;

  nlohmann::json equipped = nlohmann::json::array();
  for (size_t i = 0; i < kArmorSlots; ++i)
  {
    const InventoryEntryRef &e = EquippedArmor[i];
    nlohmann::json s;
    s["empty"] = e.empty;
    if (!e.empty)
    {
      s["kind"] = "item";
      s["id"] = e.Id;
      s["wear"] = e.wear;
      s["broken"] = e.broken;
      s["count"] = e.count;
    }
    equipped.push_back(s);
  }
  out["equipped_armor"] = equipped;

  {
    nlohmann::json oh;
    oh["empty"] = EquippedOffhand.empty;
    if (!EquippedOffhand.empty)
    {
      switch (EquippedOffhand.kind)
      {
      case InventoryEntryKind::Block:
        oh["kind"] = "block";
        break;
      case InventoryEntryKind::Item:
      default:
        oh["kind"] = "item";
        break;
      }
      oh["id"] = EquippedOffhand.Id;
      oh["count"] = EquippedOffhand.count;
      if (EquippedOffhand.kind == InventoryEntryKind::Item)
      {
        oh["wear"] = EquippedOffhand.wear;
        oh["broken"] = EquippedOffhand.broken;
      }
    }
    out["equipped_offhand"] = oh;
  }

  nlohmann::json groups = nlohmann::json::object();
  for (const auto &pair : EquippedArmorGroups.Ratings)
  {
    groups[pair.first] = pair.second;
  }
  out["equipped_armor_groups"] = groups;
}

void UCreatureInventory::DeserializeFromJson(const nlohmann::json &data,
                                             size_t maxBarCount)
{
  Storage.clear();
  if (data.contains("storage") && data["storage"].is_object())
  {
    for (auto it = data["storage"].begin(); it != data["storage"].end(); ++it)
    {
      Storage[it.key()] = it.value().get<int>();
    }
  }
  Hotbars.clear();
  if (data.contains("hotbars") && data["hotbars"].is_array())
  {
    for (const auto &barJson : data["hotbars"])
    {
      if (Hotbars.size() >= maxBarCount)
      {
        break;
      }
      HotbarBar bar;
      if (barJson.is_array())
      {
        size_t si = 0;
        for (const auto &slotJson : barJson)
        {
          if (si >= kHotbarSlots)
          {
            break;
          }
          const std::string id = slotJson.value("id", "");
          const bool slotEmpty =
              slotJson.contains("empty") ? slotJson["empty"].get<bool>() : id.empty();
          bar.slots[si].empty = slotEmpty;
          if (!bar.slots[si].empty)
          {
            const std::string kind = slotJson.value("kind", "block");
            if (kind == "object")
            {
              bar.slots[si].entry.kind = InventoryEntryKind::Object;
            }
            else if (kind == "creature")
            {
              bar.slots[si].entry.kind = InventoryEntryKind::UCreature;
            }
            else if (kind == "skin")
            {
              bar.slots[si].entry.kind = InventoryEntryKind::Skin;
            }
            else if (kind == "item")
            {
              bar.slots[si].entry.kind = InventoryEntryKind::Item;
            }
            else
            {
              bar.slots[si].entry.kind = InventoryEntryKind::Block;
            }
            bar.slots[si].entry.Id = id;
            bar.slots[si].entry.count = slotJson.value("count", 0);
            bar.slots[si].entry.wear = slotJson.value("wear", 0.f);
            bar.slots[si].entry.broken = slotJson.value("broken", false);
            bar.slots[si].entry.empty = false;
            RemapLegacyHotbarEntryId(bar.slots[si].entry);
          }
          ++si;
        }
      }
      Hotbars.push_back(bar);
    }
  }
  if (Hotbars.empty())
  {
    Hotbars.resize(1);
  }
  ActiveBarIndex = std::min(data.value("active_bar", 0u),
                            static_cast<unsigned>(Hotbars.size() - 1));
  ActiveSlotIndex = data.value("active_slot", 0u);
  if (ActiveSlotIndex >= kHotbarSlots)
  {
    ActiveSlotIndex = 0;
  }

  EquippedArmor = {};
  EquippedArmorGroups.Ratings.clear();

  if (data.contains("equipped_armor") && data["equipped_armor"].is_array())
  {
    const auto &arr = data["equipped_armor"];
    const size_t n = std::min(static_cast<size_t>(arr.size()), kArmorSlots);
    for (size_t i = 0; i < n; ++i)
    {
      const auto &ej = arr[i];
      const bool empty = ej.value("empty", ej.value("id", std::string()).empty());
      EquippedArmor[i].empty = empty;
      if (!empty)
      {
        EquippedArmor[i].kind = InventoryEntryKind::Item;
        EquippedArmor[i].Id = ej.value("id", "");
        EquippedArmor[i].count = ej.value("count", 1);
        EquippedArmor[i].wear = ej.value("wear", 0.f);
        EquippedArmor[i].broken = ej.value("broken", false);
        EquippedArmor[i].empty = EquippedArmor[i].Id.empty();
      }
    }
  }

  if (data.contains("equipped_armor_groups") &&
      data["equipped_armor_groups"].is_object())
  {
    for (auto it = data["equipped_armor_groups"].begin();
         it != data["equipped_armor_groups"].end(); ++it)
    {
      if (it.value().is_number_integer() || it.value().is_number_unsigned())
      {
        EquippedArmorGroups.Ratings[it.key()] = it.value().get<int>();
      }
      else if (it.value().is_number_float())
      {
        EquippedArmorGroups.Ratings[it.key()] =
            static_cast<int>(std::lround(it.value().get<float>()));
      }
    }
  }

  EquippedOffhand = InventoryEntryRef{};
  if (data.contains("equipped_offhand") && data["equipped_offhand"].is_object())
  {
    const auto &oh = data["equipped_offhand"];
    const bool empty =
        oh.value("empty", oh.value("id", std::string()).empty());
    EquippedOffhand.empty = empty;
    if (!empty)
    {
      const std::string kind = oh.value("kind", "item");
      if (kind == "block")
      {
        EquippedOffhand.kind = InventoryEntryKind::Block;
      }
      else
      {
        EquippedOffhand.kind = InventoryEntryKind::Item;
      }
      EquippedOffhand.Id = oh.value("id", "");
      EquippedOffhand.count = oh.value("count", 1);
      EquippedOffhand.wear = oh.value("wear", 0.f);
      EquippedOffhand.broken = oh.value("broken", false);
      EquippedOffhand.empty = EquippedOffhand.Id.empty();
    }
  }
}

} // namespace cutum
