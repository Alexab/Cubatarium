#include "Creatures/Core/CreatureInventory.h"
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{
constexpr size_t kHotbarSlots = 10;
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

void UCreatureInventory::SetPrefabHotbar(
    const std::vector<std::string> &prefab_names)
{
  EnsureHotbarCount(2);
  size_t idx = 0;
  for (const std::string &Name : prefab_names)
  {
    if (idx >= kHotbarSlots)
    {
      break;
    }
    InventoryEntryRef entry;
    entry.kind = InventoryEntryKind::UObject;
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

const std::string &UCreatureInventory::GetActivePrefabName() const
{
  static const std::string kEmpty;
  const InventoryEntryRef *Active = GetActiveEntryRef();
  if (Active && !Active->empty && Active->kind == InventoryEntryKind::UObject)
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
        case InventoryEntryKind::UObject:
          s["kind"] = "object";
          break;
        case InventoryEntryKind::UCreature:
          s["kind"] = "creature";
          break;
        case InventoryEntryKind::Skin:
          s["kind"] = "skin";
          break;
        }
        s["id"] = slot.entry.Id;
        s["count"] = slot.entry.count;
      }
      slots.push_back(s);
    }
    bars.push_back(slots);
  }
  out["hotbars"] = bars;
  out["active_bar"] = ActiveBarIndex;
  out["active_slot"] = ActiveSlotIndex;
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
          bar.slots[si].empty = slotJson.value("empty", true);
          if (!bar.slots[si].empty)
          {
            const std::string kind = slotJson.value("kind", "block");
            if (kind == "object")
            {
              bar.slots[si].entry.kind = InventoryEntryKind::UObject;
            }
            else if (kind == "creature")
            {
              bar.slots[si].entry.kind = InventoryEntryKind::UCreature;
            }
            else if (kind == "skin")
            {
              bar.slots[si].entry.kind = InventoryEntryKind::Skin;
            }
            else
            {
              bar.slots[si].entry.kind = InventoryEntryKind::Block;
            }
            bar.slots[si].entry.Id = slotJson.value("id", "");
            bar.slots[si].entry.count = slotJson.value("count", 0);
            bar.slots[si].entry.empty = false;
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
}

} // namespace cutum
