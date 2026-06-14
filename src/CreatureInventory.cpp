#include "CreatureInventory.h"
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{
constexpr size_t kHotbarSlots = 10;
}

void UCreatureInventory::AddItem(const std::string &id, int count)
{
  storage_[id] += count;
}

void UCreatureInventory::AddToInventory(const std::string &id)
{
  if (storage_[id] < 0)
  {
    return;
  }
  storage_[id]++;
}

void UCreatureInventory::InitCreativeDefaults()
{
  static const char *kBlocks[] = {
      "wood",      "grass",      "stone", "tree_birch", "pumpkin",
      "sandstone", "stonebrick", "tnt",   "brick",      "bedrock",
      "water",     "lava",       "fire"};
  for (const char *id : kBlocks)
  {
    storage_[id] = -1;
  }
}

void UCreatureInventory::EnsureDefaultHotbar()
{
  EnsureHotbarCount(1);
  bool hasBlock = false;
  for (const auto &slot : GetHotbar(0).slots)
  {
    if (!slot.empty && slot.entry.kind == InventoryEntryKind::Block &&
        !slot.entry.id.empty())
    {
      hasBlock = true;
      break;
    }
  }
  if (!hasBlock)
  {
    InventoryEntryRef wood;
    wood.kind = InventoryEntryKind::Block;
    wood.id = "wood";
    wood.empty = false;
    wood.count = -1;
    AssignToHotbar(0, 1, wood);
  }
  SetActiveSlot(0, 1);
}

void UCreatureInventory::SetPrefabHotbar(
    const std::vector<std::string> &prefab_names)
{
  EnsureHotbarCount(2);
  size_t idx = 0;
  for (const std::string &name : prefab_names)
  {
    if (idx >= kHotbarSlots)
    {
      break;
    }
    InventoryEntryRef entry;
    entry.kind = InventoryEntryKind::UObject;
    entry.id = name;
    entry.empty = false;
    entry.count = 0;
    AssignToHotbar(1, idx, entry);
    ++idx;
  }
}

const HotbarBar &UCreatureInventory::GetHotbar(size_t bar) const
{
  static const HotbarBar kEmpty{};
  if (bar >= hotbars_.size())
  {
    return kEmpty;
  }
  return hotbars_[bar];
}

void UCreatureInventory::ClearHotbarSlot(size_t bar, size_t slot)
{
  if (bar >= hotbars_.size() || slot >= kHotbarSlots)
  {
    return;
  }
  hotbars_[bar].slots[slot] = HotbarSlot{};
}

bool UCreatureInventory::SetActiveSlot(size_t bar, size_t slot)
{
  if (bar >= hotbars_.size() || slot >= kHotbarSlots)
  {
    return false;
  }
  activeBarIndex_ = bar;
  activeSlotIndex_ = slot;
  return true;
}

size_t UCreatureInventory::GetActiveSlotIndex(size_t bar) const
{
  if (bar == activeBarIndex_)
  {
    return activeSlotIndex_;
  }
  return kHotbarSlots;
}

const std::string &UCreatureInventory::GetActiveBlockTypeName() const
{
  static const std::string kEmpty;
  const InventoryEntryRef *active = GetActiveEntryRef();
  if (active && !active->empty && active->kind == InventoryEntryKind::Block)
  {
    return active->id;
  }
  return kEmpty;
}

const std::string &UCreatureInventory::GetActivePrefabName() const
{
  static const std::string kEmpty;
  const InventoryEntryRef *active = GetActiveEntryRef();
  if (active && !active->empty && active->kind == InventoryEntryKind::UObject)
  {
    return active->id;
  }
  return kEmpty;
}

const InventoryEntryRef *UCreatureInventory::GetActiveEntryRef() const
{
  if (hotbars_.empty() || activeBarIndex_ >= hotbars_.size())
  {
    return nullptr;
  }
  const auto &bar = hotbars_[activeBarIndex_];
  if (activeSlotIndex_ >= bar.slots.size())
  {
    return nullptr;
  }
  const auto &slot = bar.slots[activeSlotIndex_];
  if (slot.empty)
  {
    return nullptr;
  }
  return &slot.entry;
}

void UCreatureInventory::EnsureHotbarCount(size_t count)
{
  if (hotbars_.size() >= count)
  {
    return;
  }
  while (hotbars_.size() < count)
  {
    hotbars_.push_back(HotbarBar{});
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
  hotbars_[bar].slots[slot].empty = entry.empty;
  hotbars_[bar].slots[slot].entry = entry;
  return true;
}

void UCreatureInventory::SerializeToJson(nlohmann::json &out) const
{
  out["storage"] = storage_;
  nlohmann::json bars = nlohmann::json::array();
  for (const auto &bar : hotbars_)
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
        s["id"] = slot.entry.id;
        s["count"] = slot.entry.count;
      }
      slots.push_back(s);
    }
    bars.push_back(slots);
  }
  out["hotbars"] = bars;
  out["active_bar"] = activeBarIndex_;
  out["active_slot"] = activeSlotIndex_;
}

void UCreatureInventory::DeserializeFromJson(const nlohmann::json &data,
                                             size_t maxBarCount)
{
  storage_.clear();
  if (data.contains("storage") && data["storage"].is_object())
  {
    for (auto it = data["storage"].begin(); it != data["storage"].end(); ++it)
    {
      storage_[it.key()] = it.value().get<int>();
    }
  }
  hotbars_.clear();
  if (data.contains("hotbars") && data["hotbars"].is_array())
  {
    for (const auto &barJson : data["hotbars"])
    {
      if (hotbars_.size() >= maxBarCount)
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
            bar.slots[si].entry.id = slotJson.value("id", "");
            bar.slots[si].entry.count = slotJson.value("count", 0);
            bar.slots[si].entry.empty = false;
          }
          ++si;
        }
      }
      hotbars_.push_back(bar);
    }
  }
  if (hotbars_.empty())
  {
    hotbars_.resize(1);
  }
  activeBarIndex_ = std::min(data.value("active_bar", 0u),
                             static_cast<unsigned>(hotbars_.size() - 1));
  activeSlotIndex_ = data.value("active_slot", 0u);
  if (activeSlotIndex_ >= kHotbarSlots)
  {
    activeSlotIndex_ = 0;
  }
}

} // namespace cutum
