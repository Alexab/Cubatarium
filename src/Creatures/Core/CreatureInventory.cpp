#include "Creatures/Core/CreatureInventory.h"
#include "Audio/AudioApi.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
#include <climits>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

namespace cutum
{
namespace
{
const char *ArmorId(size_t i)
{
  static const char *ids[]{"head", "chest", "legs", "feet"};
  return i < 4 ? ids[i] : "";
}
const char *KindName(InventoryEntryKind k)
{
  switch (k)
  {
  case InventoryEntryKind::Item:
    return "item";
  case InventoryEntryKind::Object:
    return "object";
  case InventoryEntryKind::UCreature:
    return "creature";
  case InventoryEntryKind::Skin:
    return "skin";
  default:
    return "block";
  }
}
InventoryEntryKind ReadKind(const std::string &s)
{
  if (s == "item")
    return InventoryEntryKind::Item;
  if (s == "object")
    return InventoryEntryKind::Object;
  if (s == "creature")
    return InventoryEntryKind::UCreature;
  if (s == "skin")
    return InventoryEntryKind::Skin;
  return InventoryEntryKind::Block;
}
nlohmann::json WriteEntry(const InventoryEntryRef &e)
{
  if (e.empty || e.Id.empty() || e.count == 0)
    return {{"empty", true}};
  return {{"empty", false},
          {"kind", KindName(e.kind)},
          {"id", e.Id},
          {"count", e.count},
          {"wear", e.wear},
          {"broken", e.broken},
          {"instance_id", e.instanceId},
          {"contents_id", e.contentsId},
          {"contents_units", e.contentsUnits},
          {"pinned", e.pinned}};
}
InventoryEntryRef ReadEntry(const nlohmann::json &j)
{
  InventoryEntryRef e;
  if (!j.is_object())
    return e;
  e.Id = j.value("id", std::string{});
  e.empty = j.value("empty", e.Id.empty());
  if (e.empty || e.Id.empty())
    return {};
  e.kind = ReadKind(j.value("kind", std::string{"item"}));
  e.count = j.value("count", 1);
  e.wear = j.value("wear", 0.f);
  e.wear = std::isfinite(e.wear) ? std::clamp(e.wear, 0.f, 1.f) : 1.f;
  e.broken = j.value("broken", false);
  e.instanceId = j.value("instance_id", std::uint64_t{0});
  e.contentsId = j.value("contents_id", std::string{});
  e.contentsUnits = std::max(0, j.value("contents_units", 0));
  e.pinned = j.value("pinned", false);
  return e;
}
void GearSound(AudioEventKind kind)
{
  UAudioEvent e;
  e.Kind = kind;
  UAudioApi::Post(e);
}
} // namespace

InventoryEntryRef UCreatureInventory::Prepare(InventoryEntryRef e)
{
  if (e.empty || e.Id.empty() || e.count == 0)
    return {};
  e.empty = false;
  if (e.instanceId == 0)
    e.instanceId = NextInstanceId++;
  else
    NextInstanceId = std::max(NextInstanceId, e.instanceId + 1);
  return e;
}
int UCreatureInventory::StackLimit(const InventoryEntryRef &e) const
{
  if (e.kind == InventoryEntryKind::Item)
  {
    const auto *def = Items ? Items->Get(e.Id) : nullptr;
    return def ? std::clamp(def->StackMax, 1, 999) : 1;
  }
  return e.kind == InventoryEntryKind::Block ? 64 : 1;
}
InventoryEntryKind UCreatureInventory::KindForId(const std::string &id) const
{
  return Items && Items->Get(id) ? InventoryEntryKind::Item
                                 : InventoryEntryKind::Block;
}
bool UCreatureInventory::CanMerge(const InventoryEntryRef &a,
                                  const InventoryEntryRef &b)
{
  return !a.empty && !b.empty && a.kind == b.kind && a.Id == b.Id &&
         a.wear == b.wear && a.broken == b.broken &&
         a.contentsId == b.contentsId && a.contentsUnits == b.contentsUnits &&
         !a.pinned && !b.pinned;
}
const InventoryEntryRef &UCreatureInventory::GetSlot(size_t i) const
{
  static const InventoryEntryRef empty;
  if (i >= kSlotCount)
    return empty;
  const auto &s = GetHotbars()[i / 10].slots[i % 10];
  return s.empty || s.entry.empty || s.entry.count == 0 ? empty : s.entry;
}
InventoryEntryRef *UCreatureInventory::GetSlotMutable(size_t i)
{
  if (i >= kSlotCount)
    return nullptr;
  return &GetHotbarsMutable()[i / 10].slots[i % 10].entry;
}
const std::map<std::string, int> &UCreatureInventory::GetStorage() const
{
  std::map<std::string, int> next;
  for (const auto &row : GetHotbars())
    for (const auto &s : row.slots)
    {
      if (s.empty || s.entry.empty || s.entry.count == 0)
        continue;
      auto &n = next[s.entry.Id];
      if (n < 0 || s.entry.count < 0)
        n = -1;
      else
        n = static_cast<int>(std::min<long long>(
            INT_MAX, static_cast<long long>(n) + s.entry.count));
    }
  if (next != StorageView)
    StorageView = std::move(next);
  return StorageView;
}
int UCreatureInventory::Count(InventoryEntryKind kind,
                              const std::string &id) const
{
  int n = 0;
  for (size_t i = 0; i < kSlotCount; ++i)
  {
    const auto &e = GetSlot(i);
    if (e.empty || e.kind != kind || e.Id != id)
      continue;
    if (e.count < 0)
      return -1;
    n = static_cast<int>(
        std::min<long long>(INT_MAX, static_cast<long long>(n) + e.count));
  }
  return n;
}
InventoryOperationResult UCreatureInventory::Insert(InventoryEntryRef e)
{
  if (e.empty || e.Id.empty() || e.count <= 0)
    return {0, std::max(0, e.count), "invalid_stack"};
  const int original = e.count;
  const int limit = StackLimit(e);
  auto &rows = GetHotbarsMutable();
  if (limit > 1)
    for (auto &row : rows)
      for (auto &s : row.slots)
      {
        if (s.empty || !CanMerge(s.entry, e) || s.entry.count < 0)
          continue;
        const int n = std::min(e.count, std::max(0, limit - s.entry.count));
        s.entry.count += n;
        e.count -= n;
      }
  bool first = true;
  for (auto &row : rows)
    for (auto &s : row.slots)
    {
      if (e.count == 0)
        break;
      if (!s.empty && !s.entry.empty && s.entry.count != 0)
        continue;
      auto part = e;
      part.count = std::min(limit, e.count);
      if (!first)
        part.instanceId = 0;
      part = Prepare(part);
      s = {false, part};
      e.count -= part.count;
      first = false;
    }
  return {original - e.count, e.count, e.count ? "inventory_full" : ""};
}
InventoryOperationResult UCreatureInventory::Take(size_t i, int count,
                                                  InventoryEntryRef &out)
{
  out = {};
  if (i >= kSlotCount || count <= 0)
    return {0, std::max(0, count), "invalid_slot"};
  auto &s = GetHotbarsMutable()[i / 10].slots[i % 10];
  if (s.empty || s.entry.empty || s.entry.count == 0)
    return {0, count, "empty_slot"};
  if (s.entry.count >= 0 && s.entry.count < count)
    return {0, count, "insufficient_items"};
  out = s.entry;
  out.count = count;
  if (Creative || s.entry.count < 0)
  {
    out.instanceId = 0;
    out = Prepare(out);
    return {count, 0, {}};
  }
  if (s.entry.count == count)
    s = {};
  else
  {
    s.entry.count -= count;
    out.instanceId = 0;
    out = Prepare(out);
  }
  return {count, 0, {}};
}
InventoryOperationResult UCreatureInventory::Move(size_t from, size_t to,
                                                  int count)
{
  if (from >= kSlotCount || to >= kSlotCount)
    return {0, 0, "invalid_slot"};
  const auto source = GetSlot(from);
  if (source.empty)
    return {0, 0, "empty_slot"};
  if (from == to)
    return {};
  const int wanted = count < 0 ? source.count : count;
  if (wanted <= 0 && !Creative)
    return {0, 0, "invalid_count"};
  if (!Creative && wanted > source.count)
    return {0, wanted, "insufficient_items"};
  auto &rows = GetHotbarsMutable();
  auto &a = rows[from / 10].slots[from % 10];
  auto &b = rows[to / 10].slots[to % 10];
  if (b.empty || b.entry.empty)
  {
    if (count < 0 || wanted == source.count || Creative)
    {
      b = a;
      a = {};
      return {source.count, 0, {}};
    }
    InventoryEntryRef part;
    auto r = Take(from, wanted, part);
    if (!r)
      return r;
    b = {false, part};
    return r;
  }
  if (StackLimit(source) > 1 && CanMerge(source, b.entry) && source.count > 0 &&
      b.entry.count > 0)
  {
    const int n = std::min(wanted, StackLimit(source) - b.entry.count);
    if (n <= 0)
      return {0, wanted, "stack_full"};
    b.entry.count += n;
    a.entry.count -= n;
    if (a.entry.count == 0)
      a = {};
    return {n, wanted - n, wanted > n ? "stack_full" : ""};
  }
  if (count >= 0 && count != source.count)
    return {0, wanted, "incompatible_stack"};
  std::swap(a, b);
  return {source.count, 0, {}};
}
InventoryOperationResult UCreatureInventory::Consume(InventoryEntryKind kind,
                                                     const std::string &id,
                                                     int count)
{
  if (count <= 0)
    return {0, 0, "invalid_count"};
  if (Creative)
    return {count, 0, {}};
  const int available = Count(kind, id);
  if (available < count)
    return {0, count, "insufficient_items"};
  int left = count;
  const size_t active = ActiveBarIndex * 10 + ActiveSlotIndex;
  const auto consume = [&](size_t i)
  {
    auto &s = Hotbars[i / 10].slots[i % 10];
    if (left <= 0 || s.empty || s.entry.empty || s.entry.kind != kind ||
        s.entry.Id != id)
      return;
    const int n = std::min(left, s.entry.count);
    s.entry.count -= n;
    left -= n;
    if (s.entry.count == 0)
      s = {};
  };
  consume(active);
  for (size_t i = 0; i < kSlotCount; ++i)
    if (i != active)
      consume(i);
  return {count, 0, {}};
}
bool UCreatureInventory::AssignOwned(size_t bar, size_t slot,
                                     const InventoryEntryRef &entry)
{
  if (bar >= 4 || slot >= 10 || entry.empty)
    return false;
  for (size_t i = 0; i < kSlotCount; ++i)
  {
    const auto &e = GetSlot(i);
    if (e.empty || e.kind != entry.kind || e.Id != entry.Id)
      continue;
    if (entry.instanceId != 0 && e.instanceId != entry.instanceId)
      continue;
    return static_cast<bool>(Move(i, bar * 10 + slot));
  }
  return false;
}
void UCreatureInventory::AddItem(const std::string &id, int count)
{
  InventoryEntryRef e;
  e.empty = false;
  e.Id = id;
  e.count = count;
  e.kind = Items && Items->Get(id) ? InventoryEntryKind::Item
                                   : InventoryEntryKind::Block;
  const auto r = Insert(e);
  if (r.remainder > 0)
  {
    e.count = r.remainder;
    Recovery.push_back(Prepare(e));
  }
}
void UCreatureInventory::SetItemDefinitions(const UItemDefinitionStorage *items)
{
  if (Items == items)
    return;
  Items = items;
  if (!Items)
    return;
  std::vector<InventoryEntryRef> excess;
  for (auto &row : Hotbars)
    for (auto &s : row.slots)
    {
      if (s.empty || s.entry.empty)
        continue;
      if (s.entry.kind == InventoryEntryKind::Block && Items->Get(s.entry.Id))
        s.entry.kind = InventoryEntryKind::Item;
      if (s.entry.kind == InventoryEntryKind::Item && !Items->Get(s.entry.Id))
      {
        Recovery.push_back(s.entry);
        s = {};
        continue;
      }
      int limit = StackLimit(s.entry);
      if (s.entry.count > limit)
      {
        auto e = s.entry;
        e.count -= limit;
        e.instanceId = 0;
        s.entry.count = limit;
        excess.push_back(e);
      }
    }
  for (auto e : excess)
  {
    const auto r = Insert(e);
    if (r.remainder)
    {
      e.count = r.remainder;
      Recovery.push_back(Prepare(e));
    }
  }
  RefreshEquipment(*Items);
}
void UCreatureInventory::SetCreative(bool creative)
{
  Creative = creative;
  if (Items)
    RefreshEquipment(*Items);
}
void UCreatureInventory::InitCreativeDefaults()
{
  SetCreative(true);
  if (CreativeHotbars[0].slots[1].empty)
  {
    InventoryEntryRef e;
    e.Id = "wood";
    e.kind = InventoryEntryKind::Block;
    e.empty = false;
    e.count = -1;
    AssignToHotbar(0, 1, e);
    SetActiveSlot(0, 1);
  }
}
void UCreatureInventory::EnsureDefaultHotbar()
{
  if (Creative && IsPrimaryHotbarEmpty())
    InitCreativeDefaults();
}
bool UCreatureInventory::IsPrimaryHotbarEmpty() const
{
  for (const auto &s : GetHotbar(0).slots)
    if (!s.empty && !s.entry.empty)
      return false;
  return true;
}
void UCreatureInventory::SetObjectHotbar(const std::vector<std::string> &names)
{
  for (size_t i = 0; i < std::min(size_t{10}, names.size()); ++i)
  {
    InventoryEntryRef e;
    e.Id = names[i];
    e.kind = InventoryEntryKind::Object;
    e.empty = false;
    e.count = -1;
    if (Creative)
      AssignToHotbar(1, i, e);
  }
}
const HotbarBar &UCreatureInventory::GetHotbar(size_t bar) const
{
  static const HotbarBar empty;
  return bar < 4 ? GetHotbars()[bar] : empty;
}
bool UCreatureInventory::AssignToHotbar(size_t bar, size_t slot,
                                        const InventoryEntryRef &entry)
{
  if (bar >= 4 || slot >= 10)
    return false;
  auto e = Prepare(entry);
  if (!e.empty && e.count > StackLimit(e) && !Creative)
    return false;
  if (Creative && !e.empty)
    e.count = -1;
  GetHotbarsMutable()[bar].slots[slot] = {e.empty, e};
  return true;
}
void UCreatureInventory::ClearHotbarSlot(size_t bar, size_t slot)
{
  if (bar < 4 && slot < 10)
    GetHotbarsMutable()[bar].slots[slot] = {};
}
bool UCreatureInventory::SetActiveSlot(size_t bar, size_t slot)
{
  if (bar >= 4 || slot >= 10)
    return false;
  ActiveBarIndex = bar;
  ActiveSlotIndex = slot;
  return true;
}
size_t UCreatureInventory::GetActiveSlotIndex(size_t bar) const
{
  return bar == ActiveBarIndex ? ActiveSlotIndex : 10;
}
const InventoryEntryRef *UCreatureInventory::GetActiveEntryRef() const
{
  const auto &e = GetSlot(ActiveBarIndex * 10 + ActiveSlotIndex);
  return e.empty ? nullptr : &e;
}
InventoryEntryRef *UCreatureInventory::GetActiveEntryRef()
{
  return const_cast<InventoryEntryRef *>(
      static_cast<const UCreatureInventory *>(this)->GetActiveEntryRef());
}
const std::string &UCreatureInventory::GetActiveBlockTypeName() const
{
  static const std::string empty;
  const auto *e = GetActiveEntryRef();
  return e && e->kind == InventoryEntryKind::Block ? e->Id : empty;
}
const std::string &UCreatureInventory::GetActiveObjectName() const
{
  static const std::string empty;
  const auto *e = GetActiveEntryRef();
  return e && e->kind == InventoryEntryKind::Object ? e->Id : empty;
}
const InventoryEntryRef &UCreatureInventory::GetEquippedArmor(size_t i) const
{
  static const InventoryEntryRef empty;
  return i < 4 ? Armor()[i] : empty;
}
const InventoryEntryRef &UCreatureInventory::GetEquippedOffhand() const
{
  return Offhand();
}
void UCreatureInventory::WearArmor(size_t slot, float delta,
                                   const UItemDefinitionStorage &items)
{
  if (slot >= 4)
    return;
  auto &e = Armor()[slot];
  const auto *def = !e.empty ? items.Get(e.Id) : nullptr;
  if (def)
    ApplyItemWear(e, *def, delta, true);
  RefreshEquipment(items);
}
void UCreatureInventory::WearOffhand(float delta,
                                     const UItemDefinitionStorage &items)
{
  auto &e = Offhand();
  const auto *def = !e.empty ? items.Get(e.Id) : nullptr;
  if (def)
    ApplyItemWear(e, *def, delta, true);
  RefreshEquipment(items);
}
void UCreatureInventory::RefreshEquipment(const UItemDefinitionStorage &items)
{
  EquippedArmorGroups.Ratings.clear();
  OffhandArmorGroups.Ratings.clear();
  for (size_t i = 0; i < 4; ++i)
  {
    const auto &e = Armor()[i];
    const auto *d = !e.empty && !e.broken ? items.Get(e.Id) : nullptr;
    if (!d || std::find(d->Armor.Slots.begin(), d->Armor.Slots.end(),
                        ArmorId(i)) == d->Armor.Slots.end())
      continue;
    for (const auto &p : d->Armor.ArmorGroups)
      EquippedArmorGroups.Ratings[p.first] += p.second;
  }
  const auto &off = Offhand();
  const auto *d = !off.empty && !off.broken ? items.Get(off.Id) : nullptr;
  if (d)
    for (const auto &p : d->Armor.ArmorGroups)
      OffhandArmorGroups.Ratings[p.first] += p.second;
}
bool UCreatureInventory::Equip(InventoryEntryRef &target,
                               const InventoryEntryRef &entry, bool consume)
{
  if (entry.empty || entry.Id.empty())
    return false;
  const auto copy = entry;
  if (target.instanceId != 0 && target.instanceId == copy.instanceId)
  {
    target = copy;
    return true;
  }
  auto trial = *this;
  InventoryEntryRef worn;
  if (consume && !Creative)
  {
    bool found = false;
    for (size_t i = 0; i < kSlotCount; ++i)
    {
      const auto &e = trial.GetSlot(i);
      if (e.empty || e.Id != copy.Id || e.kind != copy.kind ||
          (copy.instanceId && e.instanceId != copy.instanceId))
        continue;
      if (!trial.Take(i, 1, worn))
        return false;
      found = true;
      break;
    }
    if (!found)
      return false;
  }
  else
  {
    worn = trial.Prepare(copy);
    worn.count = 1;
  }
  const auto previous = target;
  if (!previous.empty && !trial.Insert(previous))
    return false;
  Hotbars = std::move(trial.Hotbars);
  CreativeHotbars = std::move(trial.CreativeHotbars);
  NextInstanceId = trial.NextInstanceId;
  target = worn;
  GearSound(AudioEventKind::Equip);
  return true;
}
bool UCreatureInventory::EquipArmor(size_t i, const InventoryEntryRef &e,
                                    const UItemDefinitionStorage &items,
                                    bool consume)
{
  if (i >= 4 || e.kind != InventoryEntryKind::Item)
    return false;
  const auto *def = items.Get(e.Id);
  if (!def || std::find(def->Armor.Slots.begin(), def->Armor.Slots.end(),
                        ArmorId(i)) == def->Armor.Slots.end())
    return false;
  SetItemDefinitions(&items);
  if (!Equip(Armor()[i], e, consume))
    return false;
  RefreshEquipment(items);
  return true;
}
bool UCreatureInventory::ReturnEquipment(InventoryEntryRef &e)
{
  if (e.empty)
    return true;
  if (!Creative)
  {
    auto trial = *this;
    if (!trial.Insert(e))
      return false;
    Hotbars = std::move(trial.Hotbars);
    NextInstanceId = trial.NextInstanceId;
  }
  e = {};
  GearSound(AudioEventKind::Unequip);
  return true;
}
void UCreatureInventory::UnequipArmor(size_t i,
                                      const UItemDefinitionStorage &items)
{
  if (i < 4)
    ReturnEquipment(Armor()[i]);
  RefreshEquipment(items);
}
bool UCreatureInventory::EquipOffhand(const InventoryEntryRef &e)
{
  if (e.kind != InventoryEntryKind::Item && e.kind != InventoryEntryKind::Block)
    return false;
  return Equip(Offhand(), e, false);
}
bool UCreatureInventory::EquipOffhand(const InventoryEntryRef &e,
                                      const UItemDefinitionStorage &items,
                                      bool consume)
{
  if (e.kind != InventoryEntryKind::Item && e.kind != InventoryEntryKind::Block)
    return false;
  SetItemDefinitions(&items);
  if (!Equip(Offhand(), e, consume))
    return false;
  RefreshEquipment(items);
  return true;
}
void UCreatureInventory::UnequipOffhand()
{
  ReturnEquipment(Offhand());
  OffhandArmorGroups.Ratings.clear();
}
void UCreatureInventory::UnequipOffhand(const UItemDefinitionStorage &items)
{
  ReturnEquipment(Offhand());
  RefreshEquipment(items);
}
bool UCreatureInventory::RepairActive(const UItemDefinitionStorage &items,
                                      std::string &reason)
{
  const auto *e = GetActiveEntryRef();
  const auto *d =
      e && e->kind == InventoryEntryKind::Item ? items.Get(e->Id) : nullptr;
  if (!d || d->Repair.Materials.empty() || (e->wear <= 0.f && !e->broken))
  {
    reason = "not_repairable";
    return false;
  }
  const auto id = e->instanceId;
  auto trial = *this;
  if (!Creative)
  {
    bool paid = false;
    for (const auto &material : d->Repair.Materials)
    {
      const auto kind = items.Get(material) ? InventoryEntryKind::Item
                                            : InventoryEntryKind::Block;
      if (trial.Consume(kind, material))
      {
        paid = true;
        break;
      }
    }
    if (!paid)
    {
      reason = "missing_repair_material";
      return false;
    }
  }
  auto *fixed = trial.GetActiveEntryRef();
  if (!fixed || fixed->instanceId != id)
  {
    reason = "repair_source_changed";
    return false;
  }
  fixed->wear = std::max(0.f, fixed->wear - d->Repair.Amount);
  fixed->broken = fixed->wear >= 1.f;
  *this = std::move(trial);
  reason.clear();
  return true;
}
void UCreatureInventory::Sort()
{
  std::vector<InventoryEntryRef> movable;
  for (size_t i = 0; i < kSlotCount; ++i)
    if (!GetSlot(i).empty && !GetSlot(i).pinned)
      movable.push_back(GetSlot(i));
  std::stable_sort(movable.begin(), movable.end(),
                   [](const auto &a, const auto &b)
                   {
                     return std::tie(a.kind, a.Id, a.wear, a.instanceId) <
                            std::tie(b.kind, b.Id, b.wear, b.instanceId);
                   });
  size_t next = 0;
  for (size_t i = 0; i < kSlotCount; ++i)
  {
    if (GetSlot(i).pinned)
      continue;
    auto &s = GetHotbarsMutable()[i / 10].slots[i % 10];
    s = next < movable.size() ? HotbarSlot{false, movable[next++]}
                              : HotbarSlot{};
  }
}
InventoryOperationResult UCreatureInventory::Recover(size_t index)
{
  if (index >= Recovery.size())
    return {0, 0, "invalid_recovery_slot"};
  auto e = Recovery[index];
  const auto r = Insert(e);
  if (r.remainder)
  {
    Recovery[index].count = r.remainder;
    if (r.transferred > 0) Recovery[index].instanceId = NextInstanceId++;
  }
  else
    Recovery.erase(Recovery.begin() + index);
  return r;
}
void UCreatureInventory::SerializeToJson(nlohmann::json &out) const
{
  out["inventory_version"] = 3;
  out["inventory_creative"] = Creative;
  out["next_instance_id"] = NextInstanceId;
  out["active_bar"] = ActiveBarIndex;
  out["active_slot"] = ActiveSlotIndex;
  const auto rows = [](const auto &bars)
  {
    auto a = nlohmann::json::array();
    for (const auto &row : bars)
      for (const auto &s : row.slots)
        a.push_back(WriteEntry(s.entry));
    return a;
  };
  out["owned_slots"] = rows(Hotbars);
  out["creative_slots"] = rows(CreativeHotbars);
  const auto armor = [](const auto &set)
  {
    auto a = nlohmann::json::array();
    for (const auto &e : set)
      a.push_back(WriteEntry(e));
    return a;
  };
  out["equipped_armor"] = armor(EquippedArmor);
  out["creative_armor"] = armor(CreativeArmor);
  out["equipped_offhand"] = WriteEntry(EquippedOffhand);
  out["creative_offhand"] = WriteEntry(CreativeOffhand);
  out["inventory_recovery"] = armor(Recovery);
  out["inventory_migration_warnings"] = MigrationWarnings;
  out.erase("storage");
  out.erase("hotbars");
  out.erase("equipped_armor_groups");
}
void UCreatureInventory::DeserializeFromJson(const nlohmann::json &data, size_t)
{
  const auto *defs = Items;
  *this = UCreatureInventory{};
  Items = defs;
  auto readArmor = [&](const char *key, auto &set)
  {
    if (!data.contains(key) || !data[key].is_array())
      return;
    for (size_t i = 0; i < std::min(set.size(), data[key].size()); ++i)
      set[i] = Prepare(ReadEntry(data[key][i]));
  };
  if (data.value("inventory_version", 0) >= 3)
  {
    NextInstanceId = std::max(std::uint64_t{1},
                              data.value("next_instance_id", std::uint64_t{1}));
    auto readRows = [&](const char *key, auto &rows)
    {
      if (!data.contains(key) || !data[key].is_array())
        return;
      size_t i = 0;
      for (const auto &j : data[key])
      {
        auto e = Prepare(ReadEntry(j));
        if (i < 40)
          rows[i / 10].slots[i % 10] = {e.empty, e};
        else if (!e.empty)
          Recovery.push_back(e);
        ++i;
      }
    };
    readRows("owned_slots", Hotbars);
    readRows("creative_slots", CreativeHotbars);
    readArmor("equipped_armor", EquippedArmor);
    readArmor("creative_armor", CreativeArmor);
    EquippedOffhand = Prepare(
        ReadEntry(data.value("equipped_offhand", nlohmann::json::object())));
    CreativeOffhand = Prepare(
        ReadEntry(data.value("creative_offhand", nlohmann::json::object())));
    if (data.contains("inventory_recovery"))
      for (const auto &j : data["inventory_recovery"])
      {
        auto e = Prepare(ReadEntry(j));
        if (!e.empty)
          Recovery.push_back(e);
      }
    MigrationWarnings =
        data.value("inventory_migration_warnings", std::vector<std::string>{});
    Creative = data.value("inventory_creative", false);
  }
  else
  {
    LegacyMigration = true;
    std::map<std::string, InventoryEntryRef> prototypes;
    std::map<std::string, int> quantities;
    if (data.contains("storage") && data["storage"].is_object())
      for (auto it = data["storage"].begin(); it != data["storage"].end(); ++it)
        if (it.value().is_number_integer())
          quantities[it.key()] = it.value().get<int>();
    std::vector<std::pair<size_t, InventoryEntryRef>> placements;
    size_t i = 0;
    if (data.contains("hotbars") && data["hotbars"].is_array())
      for (const auto &row : data["hotbars"])
      {
        if (!row.is_array())
          continue;
        for (const auto &j : row)
        {
          auto e = ReadEntry(j);
          if (!e.empty)
          {
            if (e.kind == InventoryEntryKind::UCreature)
            {
              if (e.Id == "test_mob" || e.Id == "scout")
                e.Id = "sheep";
              else if (e.Id == "brute")
                e.Id = "sand_monster";
              else if (e.Id == "drifter")
                e.Id = "wolf";
            }
            auto it = prototypes.find(e.Id);
            if (it != prototypes.end() &&
                (it->second.wear != e.wear || it->second.broken != e.broken))
              MigrationWarnings.push_back("ambiguous_wear:" + e.Id);
            if (it == prototypes.end() || e.wear > it->second.wear || e.broken)
              prototypes[e.Id] = e;
            if (!quantities.count(e.Id))
              quantities[e.Id] = e.count < 0 ? -1 : std::max(1, e.count);
            if (i < 40)
              placements.push_back({i, e});
          }
          ++i;
        }
      }
    for (auto &q : quantities)
    {
      auto e =
          prototypes.count(q.first) ? prototypes[q.first] : InventoryEntryRef{};
      e.Id = q.first;
      e.empty = false;
      e.instanceId = 0;
      if (!prototypes.count(q.first))
        e.kind = Items && Items->Get(q.first) ? InventoryEntryKind::Item
                                              : InventoryEntryKind::Block;
      if (q.second < 0)
      {
        for (const auto &p : placements)
          if (p.second.Id == q.first)
          {
            auto c = e;
            c.count = -1;
            CreativeHotbars[p.first / 10].slots[p.first % 10] = {false,
                                                                 Prepare(c)};
          }
        continue;
      }
      int left = q.second;
      std::set<size_t> assigned;
      for (const auto &p : placements)
        if (left > 0 && p.second.Id == q.first && assigned.empty())
        {
          e.count = std::min(StackLimit(e), left);
          Hotbars[p.first / 10].slots[p.first % 10] = {false, Prepare(e)};
          left -= e.count;
          assigned.insert(p.first);
        }
      if (left > 0)
      {
        e.count = left;
        const auto r = Insert(e);
        if (r.remainder)
        {
          e.count = r.remainder;
          Recovery.push_back(Prepare(e));
        }
      }
    }
    if (data.contains("equipped_armor") && data["equipped_armor"].is_array())
    {
      const auto &a = data["equipped_armor"];
      for (size_t s = 0; s < 4; ++s)
      {
        size_t old = a.size() >= 6 && s >= 2 ? s + 2 : s;
        if (old < a.size())
          EquippedArmor[s] = Prepare(ReadEntry(a[old]));
      }
      if (a.size() >= 6)
        for (size_t s : {size_t{2}, size_t{3}})
        {
          auto e = Prepare(ReadEntry(a[s]));
          if (!e.empty)
            Recovery.push_back(e);
        }
    }
    EquippedOffhand = Prepare(
        ReadEntry(data.value("equipped_offhand", nlohmann::json::object())));
  }
  ActiveBarIndex = std::min(size_t{3}, data.value("active_bar", size_t{0}));
  ActiveSlotIndex = std::min(size_t{9}, data.value("active_slot", size_t{0}));
  // Repair duplicate identities and invalid owned counts without deleting
  // payloads.
  std::set<std::uint64_t> seen;
  auto unique = [&](InventoryEntryRef &e)
  {
    if (e.empty)
      return;
    if (!seen.insert(e.instanceId).second)
    {
      e.instanceId = 0;
      e = Prepare(e);
      seen.insert(e.instanceId);
    }
  };
  for (auto &row : Hotbars)
    for (auto &s : row.slots)
    {
      if (s.entry.count < 0)
      {
        auto e = s.entry;
        e.count = -1;
        CreativeHotbars[&row - &Hotbars[0]].slots[&s - &row.slots[0]] = {false,
                                                                         e};
        s = {};
      }
      unique(s.entry);
    }
  for (auto &e : EquippedArmor)
    unique(e);
  unique(EquippedOffhand);
  for (auto &e : Recovery)
    unique(e);
  Items = nullptr;
  SetItemDefinitions(defs);
}
} // namespace cutum
